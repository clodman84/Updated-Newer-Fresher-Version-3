# UNFV3 Workflows and Call-Stacks

This document provides example call-stacks for major operations within UNFV3, illustrating the flow from user input to system execution.

## 1. Loading an Image Roll

When a user selects "Load Roll" from the menu:

```text
main()
└── render_menu_bar()
    └── SDL_ShowOpenFolderDialog() (Async Callback)
        └── load_roll_callback()
            └── AppState::pending_session_paths.push_back()

main() (Next Frame)
└── process_pending_sessions()
    └── Session::Session()
        ├── load_existing_bill() (Load saved session data)
        └── ImageManager::ImageManager()
            └── ImageManager::load_folder()
                ├── (Populate image_names list)
                └── ImageManager::load_thumbnails()
                    └── (Create SDL_GPUTextures for carousel)
```

## 2. Navigating to a New Image

What happens when the user clicks a thumbnail or presses an arrow key:

```text
main()
└── render_sessions()
    └── Session::handle_keyboard_nav()
        └── ImageManager::load_next()
            └── ImageManager::apply_pending_selection()
                └── ImageEditor::load_path(image_path)
                    ├── ImageEditor::cleanup_stale_resources()
                    ├── ImageEditor::prepare_gegl_graph()
                    │   ├── gegl_buffer_new()
                    │   └── gegl_node_new_child("gegl:buffer-source")
                    └── ImageEditor::put_render_request()
```

## 3. Applying an Image Effect (e.g., Levels)

The interactive loop when a user adjusts a filter slider:

```text
main()
└── render_sessions()
    └── ImageManager::render_manager() [via Session]
        └── ImageEditor::render_controls()
            └── (Levels UI block)
                └── draw_levels_bar() (User drags handle)
                    └── ImageEditor::get_or_create_effect(EffectType::Levels)
                        ├── gegl_node_set(levels_node, "gamma", value)
                        └── ImageEditor::put_render_request()
```

## 4. Background Render Loop (Asynchronous)

Triggered after a render request is put into the queue:

```text
ImageEditor::render_thread() (Worker Loop)
└── ImageEditor::apply_gegl_texture()
    ├── gegl_node_blit(sink, ...)
    │   └── (GEGL recomputes affected graph nodes)
    ├── upload_texture_data_to_gpu(raw_pixels)
    │   ├── SDL_CreateGPUTexture()
    │   └── SDL_UploadToGPUTexture()
    └── (Queue old texture for release in textures_to_release)
```

## 5. Searching the Student Database

When a user types into the search bar:

```text
Session::render_searcher()
└── ImGui::InputText() (User input)
    └── Session::evaluate()
        ├── lex(search_query) -> std::vector<Token>
        ├── parse(tokens) -> RPN stream
        └── loop for each token:
            └── Database::search()
                ├── sqlite3_bind_text()
                └── sqlite3_step() -> populate search_results
```

## 6. Exporting Processed Images

The finalization process using a background worker:

```text
Session::render_export_modal()
└── Session::start_export()
    └── Session::export_images() (New Worker Thread)
        └── loop for each image in pending list:
            └── Session::process_pending_image()
                ├── ImageEditor::load_path()
                ├── gegl_node_blit(sink, ...) (Process full resolution)
                ├── OpenCV::putText() (Apply watermark stamp)
                └── stbi_write_jpg() (Save final image to disk)
                └── Atomic update: export_progress++
```
