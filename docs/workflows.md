# Workflows and Call-Stacks

This document provides example call-stacks for major operations within UNFV3.

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
        └── ImageManager::ImageManager()
            └── ImageManager::load_folder()
                └── ImageManager::load_thumbnails()
```

## 2. Applying an Image Effect (e.g., Exposure)

When a user adjusts the "Exposure" slider in the UI:

```text
main()
└── render_sessions()
    └── Session::render_manager() [via ImageManager]
        └── ImageEditor::render_controls()
            └── ImGui::SliderFloat("Exposure") (User Input)
                └── ImageEditor::get_or_create_effect(EffectType::Exposure)
                    └── gegl_node_set(exposure_node, "exposure", value)
                    └── ImageEditor::put_render_request()
```

## 3. Background Image Rendering

Triggered after a render request is put into the queue:

```text
ImageEditor::render_thread() (Worker Loop)
└── ImageEditor::apply_gegl_texture()
    └── gegl_node_process(sink)
    └── SDL_AcquireGPUCommandBuffer()
    └── upload_texture_data_to_gpu()
    └── SDL_SubmitGPUCommandBuffer()
```

## 4. Exporting Processed Images

When the user initiates an export:

```text
Session::render_export_modal()
└── Session::start_export()
    └── Session::export_images() (New Thread)
        └── Session::process_pending_image()
            └── ImageEditor::load_path()
            └── gegl_node_process(file_sink)
            └── [Optional] OpenCV Watermarking
```
