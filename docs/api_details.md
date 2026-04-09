# UNFV3 API Details

This document provides a technical deep-dive into the classes and methods that define the UNFV3 API.

---

## 1. `ImageEditor` Class
Responsible for the GEGL processing graph and the preview rendering lifecycle.

### Key Methods:
- **`void load_path(std::filesystem::path path)`**
  - Triggers a full reset of the editing state.
  - Calls `prepare_gegl_graph()` which initializes the `image_buffer` (wrapping the raw RGBA pixels) and the `source` and `sink` nodes.
- **`void render_controls()`**
  - An extensive method generating ImGui blocks for every supported GEGL effect.
  - Uses `gegl_has_operation()` to check for plugin availability before drawing UI.
  - Implements the "Reset" logic which reverts state structs to defaults and calls `gegl_node_set`.
- **`Effect& get_or_create_effect(EffectType type)`**
  - Manages the topological order of the GEGL graph.
  - If the effect doesn't exist, it creates the node, links it between the current "last node" and the "sink", and updates the `effects` vector.
- **`void remove_effect(EffectType type)`**
  - Handles the complex task of "unlinking" a node from the GEGL chain.
  - Re-links the predecessor to the successor before removing the node from the graph.
- **`void apply_gegl_texture(RenderRequest req)`**
  - The core of the background thread.
  - Executes `gegl_node_blit` with the specified `scale` (zoom).
  - Uses `upload_texture_data_to_gpu` to move the results into a new `SDL_GPUTexture`.

---

## 2. `Session` Class
Manages the workflow for a specific roll, student data association, and exporting.

### Key Methods:
- **`void evaluate()`**
  - The entry point for the custom search engine.
  - Orchestrates `lex()` and `parse()` to convert a user's search string into a Reverse Polish Notation (RPN) token stream.
  - Evaluates the RPN stream, performing set operations (`intersection` for AND, `union` for OR) on the results returned from `Database::search()`.
- **`void start_export()`**
  - Spawns the `export_worker` thread.
  - Sets `exporting = true` and initializes progress atomics.
- **`void process_pending_image(const PendingImage &image, size_t worker_index)`**
  - Runs in the background.
  - Loads the image into a temporary GEGL graph, applies all effects stored in the session, stamps watermarks using OpenCV's `putText` or `rectangle` functions, and writes the output.

---

## 3. `Database` Class
Wraps the SQLite3 backend for student record management.

### Key Methods:
- **`void read_csv(const std::string &filename)`**
  - Manually parses CSV data, handling quoted strings and various delimiters.
  - Stores parsed rows in the `loaded` vector for UI review.
- **`void insert_data()`**
  - Uses a SQL transaction (`BEGIN` / `COMMIT`) to insert thousands of records into the `mess_list` and `mess_list_fts` tables efficiently.
- **`void search(TokenType search_type, std::string search_query, results)`**
  - Binds the query to prepared statements (`fts_search`, `bhawan_search`, or `id_search`).
  - Iterates through the result set using `sqlite3_step()` to populate the UI result table.

---

## 4. `ImageManager` Class
Handles file-system navigation and carousel UI.

### Key Methods:
- **`void render_manager()`**
  - Calculates the layout for the viewer and carousel.
  - Manages the `pending_index` to smoothly transition between images without blocking the UI during I/O.
- **`void load_thumbnails()`**
  - Iterates through `image_names`, loads a downsampled version of each image, and creates a small `SDL_GPUTexture` stored in the `thumbnails` map.
- **`void render_carousel(float carousel_height)`**
  - A specialized ImGui loop that draws the horizontal scroll bar of thumbnails.
  - Uses `render_thumbnail_item` to handle individual thumbnail clicks and selection highlighting.
