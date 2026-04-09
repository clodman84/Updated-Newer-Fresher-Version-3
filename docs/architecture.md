# UNFV3 Software Architecture

UNFV3 is a sophisticated cross-platform image editing and management suite. Its architecture is engineered for high-performance, non-destructive editing and efficient handling of large student record databases.

## Core Pillars

1.  **SDL3 GPU Abstraction**: All rendering and texture management are handled via the modern SDL3 GPU API. This provides a low-level, high-performance interface to the hardware while abstracting away platform-specific APIs like Vulkan, Metal, or DX12.
2.  **GEGL (Generic Graphics Library)**: The core processing engine. It utilizes a directed acyclic graph (DAG) of operations to provide non-destructive editing. This allows for complex filter chains that can be re-evaluated lazily.
3.  **Dear ImGui**: The immediate-mode user interface is tightly integrated with the SDL3 GPU backend, ensuring that UI rendering is synchronized with image preview updates.
4.  **SQLite Search Engine**: A custom-built search system leveraging SQLite FTS5 to provide instantaneous lookups across thousands of student records.

## Component Architecture

```text
+-----------------------------------------------------------+
|                      Main UI Loop                         |
|      (SDL Event Polling, ImGui Frame, GPU Command Submission) |
+-------------+---------------------------+-----------------+
              |                           |
              v                           v
+-----------------------------+ +---------------------------+
|       Session Manager       | |      Image Processing      |
|                             | |                           |
|  +-----------------------+  | |  +---------------------+  |
|  |  Database (SQLite)    |  | |  |  ImageEditor (GEGL) |  |
|  +-----------------------+  | |  +---------------------+  |
|  |  ImageManager (Nav)   |  | |  |  Render Thread      |  |
|  +-----------------------+  | |  +---------------------+  |
+-------------+---------------+ +-------------+-------------+
              |                               |
              +---------------+---------------+
                              |
                              v
                +----------------------------+
                |     SDL3 GPU Device        |
                | (Textures, Command Buffers)|
                +----------------------------+
```

## Deep Dive: System Interactions

### 1. Non-Destructive Image Pipeline
UNFV3 does not modify the source image. Instead, it constructs a GEGL graph:
- **Source Node**: A `gegl:buffer-source` wrapping the raw pixels.
- **Filter Chain**: A sequence of `GeglNode` objects (e.g., `gegl:exposure`, `unfv3:gimp-levels`).
- **Sink Node**: A `gegl:nop` node that serves as the pull point for the render thread.

When a user modifies a parameter, only the relevant node's properties are changed, and GEGL's dirty-tracking ensures only affected regions are recomputed.

### 2. Multi-Threaded Rendering & GPU Sync
Heavy image processing is decoupled from the UI thread to ensure 60+ FPS interaction:
- **UI Thread**: Handles input and submits GPU commands for the final frame.
- **GEGL Thread**: Pulls pixels from the GEGL sink at the requested zoom level and ROI.
- **Deferred Release**: To prevent GPU crashes, textures that are replaced (e.g., during rapid slider movement) are added to a `textures_to_release` queue and destroyed only at the start of the next frame when the GPU is known to be idle for those resources.

### 3. Data Persistence and Export
- **Session State**: Billed entries and image metadata are serialized to JSON for "autosave" functionality.
- **Export Engine**: A multi-threaded worker that clones the `ImageEditor` state for each image in the bill, processes them at full resolution, applies watermarks via OpenCV, and saves the final artifacts to disk using `stbi_write`.

## Data Flow Summary

1.  **Ingestion**: Source pixels are loaded via `stb_image`, converted to the appropriate Babl format, and uploaded to a `GeglBuffer`.
2.  **Manipulation**: UI events trigger property changes on `GeglNode` objects.
3.  **Visualization**: The render thread calls `gegl_node_blit`, pulls processed pixels into a buffer, and then uploads that buffer to an `SDL_GPUTexture`.
4.  **Search**: Queries are lexed and parsed into RPN, then executed as SQL queries against FTS5 virtual tables.
