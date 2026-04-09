# API Documentation

This document describes the primary classes and methods in the UNFV3 codebase.

## `ImageEditor`

The central class for image manipulation.

### Methods
- `load_path(std::filesystem::path)`: Loads an image from disk into the GEGL processing graph.
- `render_controls()`: Renders the ImGui UI for all active image effects.
- `render_preview()`: Orchestrates the rendering of the processed image to the screen.
- `get_or_create_effect(EffectType type)`: Manages the lifecycle of GEGL nodes in the processing chain.

## `Session`

Manages a workspace consisting of an image folder and a set of search/billing data.

### Methods
- `render_searcher()`: Displays the UI for searching the database and adding images to the current session.
- `render_billed()`: Shows the list of images tagged or "billed" in the current session.
- `export_images()`: Processes and saves the final edited images to a specified directory.

## `ImageManager`

Handles the collection of images within a session folder.

### Methods
- `load_thumbnails()`: Loads small versions of all images in the folder for the carousel view.
- `render_manager()`: Displays the image viewer and the carousel UI.
- `load_next()` / `load_previous()`: Navigates through the image collection.

## `Database`

Wraps SQLite for metadata storage and retrieval.

### Methods
- `search(TokenType, std::string, ...)`: Performs a search based on full-text search (FTS), IDs, or other criteria.
- `read_csv(std::string)`: Imports metadata from a CSV file into the SQLite database.

## `GPU Utils`

Found in `gpu_utils.h/cpp`, these helper functions facilitate interaction with the SDL3 GPU API.

### Functions
- `upload_texture_data_to_gpu(...)`: Creates a GPU texture and uploads pixel data.
- `create_gpu_buffer(...)`: Allocates a buffer on the GPU.
