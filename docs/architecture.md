# Architecture Overview

UNFV3 is a cross-platform image editing and management application. Its architecture is built around three core pillars:

1.  **SDL3 GPU Abstraction**: Used for all rendering and texture management. It provides a modern, low-level API for high-performance graphics while maintaining cross-platform compatibility.
2.  **GEGL (Generic Graphics Library)**: The powerhouse behind image processing. UNFV3 utilizes GEGL's graph-based architecture to chain non-destructive image effects.
3.  **Dear ImGui**: Provides the immediate-mode user interface, integrated with SDL3's GPU backend for seamless rendering.

## High-Level Component Diagram

```text
+---------------------------------------+
|              Main Loop                |
|  (SDL Events, ImGui Frame, Render)    |
+---------+--------------------+--------+
          |                    |
          v                    v
+------------------+  +----------------------+
|     Session      |  |     ImageEditor      |
| (Management, DB) |  | (GEGL Graph, GPU Tx) |
+---------+--------+  +----------+-----------+
          |                      |
          v                      v
+------------------+  +----------------------+
|     Database     |  |    GEGL Operations   |
| (SQLite Search)  |  | (Filters & Effects)  |
+------------------+  +----------------------+
```

## Data Flow

1.  **Image Loading**: Images are loaded into `GeglBuffer` for processing and as `SDL_GPUTexture` for UI thumbnails.
2.  **Processing**: When a filter parameter is adjusted in the UI, the `ImageEditor` updates the corresponding GEGL node in the graph.
3.  **Rendering**: A background thread processes the GEGL graph and uploads the result to a GPU texture, which is then displayed by ImGui.
4.  **Database**: SQLite is used to store and search image metadata, allowing for fast retrieval and organization of large image rolls.
