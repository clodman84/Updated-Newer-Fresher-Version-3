#include "include/IconsFontAwesome6.h"
#include "include/session.h"
#include <imgui.h>

inline ImVec2 operator+(const ImVec2 &a, const ImVec2 &b) {
  return ImVec2(a.x + b.x, a.y + b.y);
}

inline ImVec2 operator+=(ImVec2 &a, const ImVec2 &b) {
  a.x += b.x;
  a.y += b.y;
  return a;
}

inline ImVec2 operator-(const ImVec2 &a, const ImVec2 &b) {
  return ImVec2(a.x - b.x, a.y - b.y);
}

inline ImVec2 operator*(const ImVec2 &a, float scalar) {
  return ImVec2(a.x * scalar, a.y * scalar);
}

inline ImVec2 operator*=(ImVec2 &a, float scalar) {
  a.x = a.x * scalar;
  a.y = a.y * scalar;
  return a;
}

void Session::reset_view_to_image() {
  const Image *image = image_manager.current_image;
  if (image == nullptr || !image->is_valid()) {
    zoom = 0.0f;
    pan = ImVec2(0.0f, 0.0f);
    return;
  }

  if (canvas_size.x > 0.0f && canvas_size.y > 0.0f) {
    zoom =
        std::min(canvas_size.x / image->width, canvas_size.y / image->height);
  } else {
    zoom = 0.0f;
  }
  pan = ImVec2(0.0f, 0.0f);
}

void ImageEditor::reset_view_to_image() {
  if (preview_texture == nullptr) {
    zoom = 1.0f;
    pan = ImVec2(0.0f, 0.0f);
    return;
  }

  if (canvas_size.x > 0.0f && canvas_size.y > 0.0f) {
    zoom = std::min(canvas_size.x / image_width, canvas_size.y / image_height);
  } else {
    zoom = 1.0f;
  }
  pan = ImVec2(0.0f, 0.0f);
}

void Session::render_main_image() {
  ImGui::TableNextColumn();
  ImGui::BeginChild("ViewerChild", ImVec2(0, 0));

  const Image *image = image_manager.current_image;
  if (image == nullptr || !image->is_valid()) {
    ImGui::TextDisabled("Failed to load image.");
    ImGui::EndChild();
    return;
  }

  ImTextureRef texture_id = image->texture;
  const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
  canvas_size = ImGui::GetContentRegionAvail();

  if (zoom <= 0.0f) {
    reset_view_to_image();
  }

  ImGui::InvisibleButton("canvas", canvas_size,
                         ImGuiButtonFlags_MouseButtonLeft);
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();

  auto io = ImGui::GetIO();
  if (hovered && io.MouseWheel != 0.0f) {
    const float old_zoom = zoom;
    zoom *= powf(1.1f, io.MouseWheel);
    zoom = std::clamp(zoom, 0.1f, 20.0f);

    ImVec2 mouse_local;
    mouse_local.x = io.MousePos.x - canvas_pos.x - pan.x;
    mouse_local.y = io.MousePos.y - canvas_pos.y - pan.y;
    const float scale = zoom / old_zoom - 1.0f;
    pan.x -= mouse_local.x * scale;
    pan.y -= mouse_local.y * scale;
  }
  if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    pan.x += io.MouseDelta.x;
    pan.y += io.MouseDelta.y;
  }

  ImVec2 image_size = {image->width * zoom, image->height * zoom};
  if (image_size.x <= canvas_size.x) {
    pan.x = (canvas_size.x - image_size.x) * 0.5f;
  } else {
    pan.x = std::clamp(pan.x, canvas_size.x - image_size.x, 0.0f);
  }
  if (image_size.y <= canvas_size.y) {
    pan.y = (canvas_size.y - image_size.y) * 0.5f;
  } else {
    pan.y = std::clamp(pan.y, canvas_size.y - image_size.y, 0.0f);
  }

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  draw_list->PushClipRect(
      canvas_pos,
      ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), true);
  const ImVec2 image_pos = {canvas_pos.x + pan.x, canvas_pos.y + pan.y};
  draw_list->AddImage(
      texture_id, image_pos,
      ImVec2(image_pos.x + image_size.x, image_pos.y + image_size.y),
      ImVec2(0, 0), ImVec2(1, 1));

  if (with_detection) {
    for (auto face :
         detector.scan_faces(image_manager.current_image->filename)) {
      draw_list->AddRect(face.bounds_min * zoom + image_pos,
                         face.bounds_max * zoom + image_pos,
                         ImGui::GetColorU32({0, 255, 0, 255}));
      draw_list->AddText({face.bounds_min.x * zoom + image_pos.x,
                          face.bounds_max.y * zoom + image_pos.y},
                         ImGui::GetColorU32({0, 255, 0, 255}),
                         std::to_string(face.count).c_str());
    }
  }

  draw_list->PopClipRect();
  ImGui::EndChild();
}

static float linked_zoom_for_target(float source_zoom, int source_width,
                                    int source_height, int target_width,
                                    int target_height) {
  if (source_width <= 0 || source_height <= 0 || target_width <= 0 ||
      target_height <= 0) {
    return source_zoom;
  }

  const float ratio_x =
      static_cast<float>(source_width) / static_cast<float>(target_width);
  const float ratio_y =
      static_cast<float>(source_height) / static_cast<float>(target_height);

  if (std::fabs(ratio_x - ratio_y) < 0.0001f) {
    return source_zoom * ratio_x;
  }
  return source_zoom * std::sqrt(ratio_x * ratio_y);
}

void Session::render_control_panel() {
  const Image *image = image_manager.current_image;

  ImGui::TableNextColumn();
  ImGui::BeginChild("Control Panel", ImVec2(0, 0));
  ImGui::Text(ICON_FA_WRENCH);
  ImGui::Separator();
  ImGui::TextUnformatted("This is a work in progress :)");

  if (image == nullptr || !image->is_valid()) {
    with_preview = false;
    with_detection = false;
    ImGui::TextDisabled("Load an image to enable editor tools.");
    ImGui::EndChild();
    return;
  }

  ImGui::Checkbox("Scan Faces", &with_detection);

  if (with_detection && image != nullptr) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.93f, 0.73f, 0.24f, 1.0f), "Face Count: %ld",
                       detector.scan_faces(image->filename).size());
  }
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_ROTATE_LEFT)) {
    reset_view_to_image();
  }
  if (ImGui::TreeNode("Edit")) {
    with_preview = true;
    // ImGui::Text("Preview Image Dimensions: %d x %d", editor->image_width,
    //             editor->image_height);
    // ImGui::Text("ROI: %d, %d, %d, %d", editor->roi.x, editor->roi.y,
    //             editor->roi.width, editor->roi.height);
    // ImGui::Text("Zoom: %f", zoom);
    // ImGui::Text("Current Texture: %d, %d, %d, %d",
    //             editor->current_texture_width,
    //             editor->current_texture_height,
    //             editor->current_texture_offset_x,
    //             editor->current_texture_offset_y);
    if (ImGui::Checkbox("Link Viewers", &link_preview_viewer) &&
        link_preview_viewer) {
      editor.set_view(linked_zoom_for_target(zoom, image->width, image->height,
                                             editor.image_width,
                                             editor.image_height),
                      pan);
    }
    editor.render_controls();
    ImGui::TreePop();
  } else
    with_preview = false;
  ImGui::EndChild();
}

void Session::render_image_panel() {
  editor.cleanup_stale_resources();
  image_manager.cleanup_stale_images();
  bool new_preview = false;

  if (with_preview && image_manager.current_image != nullptr &&
      image_manager.current_image->is_valid() &&
      (editor.image_path != image_manager.current_image->filename)) {
    editor.load_path(image_manager.current_image->filename);
    new_preview = true;
  }
  auto io = ImGui::GetIO();

  ImGui::BeginChild("ImagePanel");
  constexpr float carousel_height = 250.0f;
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float top_height = std::max(0.0f, available.y - carousel_height - 5.0f);
  ImGui::BeginChild("TopRegion", ImVec2(0, top_height),
                    ImGuiChildFlags_Borders);

  int column_count = with_preview ? 3 : 2;

  if (ImGui::BeginTable("MainLayout", column_count,
                        ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_SizingStretchProp,
                        ImVec2(0, 0))) {
    ImGui::TableSetupColumn("Viewer", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    if (with_preview)
      ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch,
                              3.0f);
    ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableNextRow();
    render_main_image();
    if (with_preview) {
      if (link_preview_viewer) {
        editor.set_view(
            linked_zoom_for_target(zoom, image_manager.current_image->width,
                                   image_manager.current_image->height,
                                   editor.image_width, editor.image_height),
            pan);
      }
      editor.render_preview();
      if (new_preview)
        editor.reset_view_to_image();
      if (link_preview_viewer) {
        zoom = linked_zoom_for_target(editor.get_zoom(), editor.image_width,
                                      editor.image_height,
                                      image_manager.current_image->width,
                                      image_manager.current_image->height);
        pan = editor.get_pan();
      }
    }
    render_control_panel();
    ImGui::EndTable();
  }

  ImGui::EndChild();
  render_carousel(carousel_height);
  ImGui::EndChild();
  last_drawn_index = image_manager.index;
}
