#include "application.h"
#include "imgui.h"
#include <cstring>
#include <misc/cpp/imgui_stdlib.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

template <typename T> static inline T Clamp(T value, T lo, T hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

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

void ImageEditor::render_preview() {
  ImGui::PushID("Preview");
  ImGui::TableNextColumn();
  ImGui::BeginChild("ViewerChild", ImVec2(0, 0));

  if (preview_texture == nullptr) {
    ImGui::TextUnformatted("Bruh Moment");
    ImGui::EndChild();
    ImGui::PopID();
    return;
  }

  ImTextureRef texture_id = preview_texture;
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
    zoom = (zoom, 0.1f, 20.0f);

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

  ImVec2 image_size = {image_width * zoom, image_height * zoom};
  if (image_size.x <= canvas_size.x) {
    pan.x = (canvas_size.x - image_size.x) * 0.5f;
  } else {
    pan.x = Clamp(pan.x, canvas_size.x - image_size.x, 0.0f);
  }
  if (image_size.y <= canvas_size.y) {
    pan.y = (canvas_size.y - image_size.y) * 0.5f;
  } else {
    pan.y = Clamp(pan.y, canvas_size.y - image_size.y, 0.0f);
  }

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  draw_list->PushClipRect(
      canvas_pos,
      ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), true);
  const ImVec2 image_pos = {canvas_pos.x + pan.x + current_texture_offset_x,
                            canvas_pos.y + pan.y + current_texture_offset_y};

  roi.x = (std::max(canvas_pos.x, image_pos.x) - image_pos.x) / zoom;
  roi.y = (std::max(canvas_pos.y, image_pos.y) - image_pos.y) / zoom;

  float roi_x2 =
      (std::min(canvas_pos.x + canvas_size.x, image_pos.x + image_size.x) -
       image_pos.x) /
      zoom;
  float roi_y2 =
      (std::min(canvas_pos.y + canvas_size.y, image_pos.y + image_size.y) -
       image_pos.y) /
      zoom;

  roi.width = roi_x2 - roi.x;
  roi.height = roi_y2 - roi.y;

  if (preview_texture != nullptr) {
    draw_list->AddImage(texture_id, image_pos,
                        ImVec2(image_pos.x + current_texture_width * zoom,
                               image_pos.y + current_texture_height * zoom),
                        ImVec2(0, 0), ImVec2(1, 1));
  }

  draw_list->PopClipRect();
  ImGui::EndChild();
  ImGui::PopID();
}

void ImageManager::render_viewer() {
  ImGui::TableNextColumn();
  ImGui::BeginChild("ViewerChild", ImVec2(0, 0));

  const Image *image = current_image_.get();
  if (image == nullptr || !image->is_valid()) {
    ImGui::TextDisabled(has_images() ? "Failed to load image."
                                     : "No images loaded.");
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
    zoom = Clamp(zoom, 0.1f, 20.0f);

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
    pan.x = Clamp(pan.x, canvas_size.x - image_size.x, 0.0f);
  }
  if (image_size.y <= canvas_size.y) {
    pan.y = (canvas_size.y - image_size.y) * 0.5f;
  } else {
    pan.y = Clamp(pan.y, canvas_size.y - image_size.y, 0.0f);
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
    for (auto face : scan_faces(current_image_->filename)) {
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

void ImageManager::render_editor() {
  const Image *image = current_image_.get();

  ImGui::TableNextColumn();
  ImGui::BeginChild("Control Panel", ImVec2(0, 0));
  ImGui::Text("Control Panel");
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
                       scan_faces(image->filename).size());
  }
  if (ImGui::Button("Reset Viewer")) {
    reset_view_to_image();
  }
  if (ImGui::TreeNode("Edit")) {
    with_preview = true;
    ImGui::Text("Preview Image Dimensions: %d x %d", editor.image_width,
                editor.image_height);
    ImGui::Text("ROI: %d, %d, %d, %d", editor.roi.x, editor.roi.y,
                editor.roi.width, editor.roi.height);
    ImGui::Text("Zoom: %f", zoom);
    ImGui::Text("Current Texture: %d, %d, %d, %d", editor.current_texture_width,
                editor.current_texture_height, editor.current_texture_offset_x,
                editor.current_texture_offset_y);
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

void ImageManager::render_carousel(float carousel_height) {
  ImGui::BeginChild("Carousel", ImVec2(0, carousel_height),
                    ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_HorizontalScrollbar);

  auto io = ImGui::GetIO();
  if (ImGui::IsWindowHovered()) {
    ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseWheel * 50.0f);
  }

  for (const auto &name : thumbnail_order) {
    const auto it = thumbnails.find(name);
    if (it == thumbnails.end() || it->second.texture == nullptr) {
      continue;
    }

    const bool is_current = !image_names.empty() && index >= 0 &&
                            index < static_cast<int>(image_names.size()) &&
                            name == image_names[index];
    if (is_current) {
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }

    ImGui::BeginGroup();
    const int frame_number = get_image_index(name) + 1;
    if (frame_number > 0) {
      ImGui::Text("Frame: %d", frame_number);
    }
    if (ImGui::ImageButton(
            name.c_str(), it->second.texture,
            ImVec2((float)it->second.width, (float)it->second.height))) {
      queue_image_by_index(get_image_index(name));
    }
    ImGui::EndGroup();

    if (is_current) {
      ImGui::PopStyleColor();
      if (index != last_drawn_index) {
        ImGui::SetScrollHereX(0.5f);
      }
    }
    ImGui::SameLine();
  }

  ImGui::EndChild();
}

void ImageManager::render_manager() {
#ifdef TRACY_ENABLE
  ZoneScopedN("ImageManager::draw_manager");
#endif
  editor.cleanup_stale_resources();
  apply_pending_selection();
  if (with_preview && current_image_ != nullptr && current_image_->is_valid() &&
      (editor.preview_texture == nullptr ||
       editor.image_path != current_image_->filename)) {
    editor.load_path(current_image_->filename);
  }
  auto io = ImGui::GetIO();

  ImGui::BeginChild("ImagePanel");
  constexpr float carousel_height = 270.0f;
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
    render_viewer();
    if (with_preview) {
      if (link_preview_viewer) {
        editor.set_view(linked_zoom_for_target(
                            zoom, current_image_->width, current_image_->height,
                            editor.image_width, editor.image_height),
                        pan);
      }
      editor.render_preview();
      if (link_preview_viewer) {
        zoom = linked_zoom_for_target(
            editor.get_zoom(), editor.image_width, editor.image_height,
            current_image_->width, current_image_->height);
        pan = editor.get_pan();
      }
    }
    render_editor();
    ImGui::EndTable();
  }

  ImGui::EndChild();
  render_carousel(carousel_height);
  ImGui::EndChild();
  last_drawn_index = index;
}

void Database::render_loaded_csv() {
  if (!show_loaded_csv) {
    return;
  }

#ifdef TRACY_ENABLE
  ZoneScopedN("Database::render_loaded_csv");
#endif
  if (!ImGui::Begin("Loaded Mess List", &show_loaded_csv)) {
    ImGui::End();
    return;
  }

  ImGui::Text("%zu CSV rows parsed. Review before importing.", loaded.size());
  if (ImGui::BeginTable("##loaded_csv", 5,
                        ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Sex", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableSetupColumn("Bhawan", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("Room", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableHeadersRow();

    for (const auto &line : loaded) {
      ImGui::TableNextRow();
      for (const auto &item : line) {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(item.c_str());
      }
    }
    ImGui::EndTable();
  }

  if (ImGui::Button("Looks good to me, load this messlist")) {
    insert_data();
  }
  ImGui::End();
}

void Session::render_searcher() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::render_searcher");
#endif
  ImGui::BeginChild("Search Window", {0.0f, 650.0f}, ImGuiChildFlags_ResizeY);

  if (focus_search_on_next_frame) {
    ImGui::SetKeyboardFocusHere();
    focus_search_on_next_frame = false;
  }

  if (ImGui::InputTextWithHint("##search_query", "Search", &search_query)) {
    evaluate();
  }
  sync_search_selection_bounds();

  ImGui::SameLine();
  if (ImGui::ArrowButton("Previous", ImGuiDir_Left)) {
    manager.load_previous();
  }
  ImGui::SameLine(0.0f, 0.0f);
  if (ImGui::ArrowButton("Next", ImGuiDir_Right)) {
    manager.load_next();
  }
  ImGui::SameLine();
  if (ImGui::Button("Same As")) {
    draw_same_as_popup = true;
    ImGui::OpenPopup("Same As Bill");
  }

  render_same_as_popup();
  render_search_results_table();
  ImGui::EndChild();
}

void Session::render_same_as_popup() {
  const auto *image_path = current_image_path();
  if (ImGui::BeginPopup("Same As Bill")) {
    ImGui::TextUnformatted("Copy billed entries from another image");
    ImGui::Separator();

    constexpr int columns = 5;
    constexpr float cell_width = 150.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float popup_width = columns * cell_width + spacing * (columns - 1);
    ImGui::SetNextWindowSize(
        ImVec2(popup_width + ImGui::GetStyle().WindowPadding.x * 2, 480.0f),
        ImGuiCond_Appearing);

    bool found_source = false;
    int column = 0;
    for (const auto &image_name : manager.get_thumbnail_order()) {
      if (image_path != nullptr && image_name == image_path->string()) {
        continue;
      }

      const Thumbnail *thumb = manager.get_thumbnail(image_name);
      if (thumb == nullptr || thumb->texture == nullptr || thumb->width <= 0) {
        continue;
      }

      found_source = true;
      if (column % columns != 0) {
        ImGui::SameLine(0.0f, spacing);
      }

      ImGui::PushID(image_name.c_str());
      ImGui::BeginGroup();

      const int frame_number = manager.get_image_index(image_name) + 1;
      if (frame_number > 0) {
        ImGui::Text("Frame %d", frame_number);
      }

      const float aspect = (float)thumb->height / (float)thumb->width;
      if (ImGui::ImageButton("##thumb", thumb->texture,
                             ImVec2(cell_width, cell_width * aspect))) {
        append_bill_from_image(image_name);
        draw_same_as_popup = false;
        ImGui::CloseCurrentPopup();
      }

      ImGui::TextUnformatted(
          std::filesystem::path(image_name).filename().string().c_str());
      ImGui::EndGroup();
      ImGui::PopID();
      ++column;
    }

    if (!found_source) {
      ImGui::TextDisabled("No other images are available.");
    }

    ImGui::Separator();
    if (ImGui::Button("Close")) {
      draw_same_as_popup = false;
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  } else if (draw_same_as_popup && !ImGui::IsPopupOpen("Same As Bill")) {
    draw_same_as_popup = false;
  }
}

void Session::render_search_results_table() {
  if (!ImGui::BeginTable("##search_results", 4,
                         ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_SizingFixedFit)) {
    return;
  }

  ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 110.0f);
  ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
  ImGui::TableSetupColumn("Bhawan", ImGuiTableColumnFlags_WidthFixed, 50.0f);
  ImGui::TableSetupColumn("Room", ImGuiTableColumnFlags_WidthFixed, 40.0f);
  ImGui::TableHeadersRow();

  for (size_t row = 0; row < search_results.size(); ++row) {
    const auto &line = search_results[row];
    ImGui::TableNextRow();
    if ((int)row == selected_search_index &&
        keyboard_nav_mode == KeyboardNavMode::Search) {
      ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                             ImGui::GetColorU32(ImGuiCol_Header));
      ImGui::SetScrollHereY();
    }

    for (int column = 0; column < 4; ++column) {
      ImGui::TableNextColumn();
      if (column == 0) {
        if (ImGui::Button(line[column].c_str())) {
          increment_for_id(line[column], line[column + 1]);
        }
      } else {
        ImGui::TextUnformatted(line[column].c_str());
      }
    }
  }

  ImGui::EndTable();
}

void Session::render_billed_table(std::map<std::string, BillEntry> &entries) {
  if (!ImGui::BeginTable("##billed_results", 3,
                         ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_SizingFixedFit)) {
    return;
  }

  ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 110.0f);
  ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
  ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 100.0f);
  ImGui::TableHeadersRow();

  int visible_index = 0;
  for (auto &[student_id, entry] : entries) {
    if (entry.count < 1) {
      continue;
    }

    ImGui::PushID(student_id.c_str());
    ImGui::TableNextRow();
    if (keyboard_nav_mode == KeyboardNavMode::Billed &&
        visible_index == selected_billed_index) {
      ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                             ImGui::GetColorU32(ImGuiCol_Header));
      ImGui::SetScrollHereY();
    }

    ImGui::TableNextColumn();
    ImGui::TextUnformatted(student_id.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(entry.name.c_str());
    ImGui::TableNextColumn();

    int count = entry.count;
    ImGui::PushItemWidth(100.0f);
    if (ImGui::InputInt("##count", &count)) {
      entry.count = std::max(count, 0);
      autosave();
    }
    ImGui::PopItemWidth();
    ImGui::PopID();
    ++visible_index;
  }

  ImGui::EndTable();
}

void Session::render_billed() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::render_billed");
#endif
  ImGui::BeginChild("Billed Window", {0.0f, 0.0f});

  if (focus_billed_on_next_frame) {
    focus_billed_on_next_frame = false;
  }

  auto *entries = current_bill_entries();
  if (entries == nullptr || visible_billed_entry_count() == 0) {
    ImGui::TextDisabled("No billed entries for the current image.");
    ImGui::EndChild();
    return;
  }

  render_billed_table(*entries);
  ImGui::EndChild();
}

void Session::render_export_modal() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::draw_export_modal");
#endif
  finish_export_if_ready();

  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
  ImGui::OpenPopup("Export Roll");

  if (!ImGui::BeginPopupModal("Export Roll", nullptr,
                              ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }

  ImGui::PushTextWrapPos(0.0f); // wrap at window edge
  ImGui::TextColored(ImVec4(0.93f, 0.73f, 0.24f, 1.0f), quote.c_str());
  ImGui::PopTextWrapPos();

  ImGui::Separator();

  ImGui::Text("Roll");
  ImGui::SameLine(150.0f);
  ImGui::TextUnformatted(path.filename().string().c_str());

  ImGui::Text("Queued images");
  ImGui::SameLine(150.0f);
  ImGui::Text("%d", export_total);

  ImGui::Text("Destination");
  ImGui::SameLine(150.0f);
  ImGui::SetNextItemWidth(340.0f);
  if (ImGui::InputText("##export_destination", &export_output_directory,
                       ImGuiInputTextFlags_AutoSelectAll) &&
      !exporting) {
    prepare_export_queue();
  }

  ImGui::Text("Options");
  ImGui::SameLine(150.0f);
  if (ImGui::Checkbox("Stamp watermark", &export_apply_watermark) &&
      !exporting) {
    export_status_message = export_apply_watermark
                                ? "Watermark stamping enabled"
                                : "Watermark stamping disabled";
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset destination") && !exporting) {
    export_output_directory.clear();
    prepare_export_queue();
  }

  ImGui::Spacing();
  ImGui::TextUnformatted(export_status_message.c_str());

  const float progress = export_total > 0
                             ? static_cast<float>(export_progress.load()) /
                                   static_cast<float>(export_total)
                             : 0.0f;
  const std::string progress_text = std::to_string(export_progress.load()) +
                                    " / " + std::to_string(export_total);
  ImGui::ProgressBar(exporting ? progress : (export_completed ? 1.0f : 0.0f),
                     ImVec2(520, 0), progress_text.c_str());

  std::vector<std::string> active_items;
  {
    std::lock_guard<std::mutex> lock(export_status_mutex);
    active_items = export_active_items;
  }

  ImGui::Spacing();
  ImGui::Text("Current image");
  ImGui::BeginChild("ExportActiveWork", ImVec2(520, 120), true);
  if (active_items.empty()) {
    ImGui::TextDisabled(exporting ? "Starting export..."
                                  : "Nothing is running right now.");
  } else {
    for (const auto &item : active_items) {
      ImGui::BulletText("%s", item.c_str());
    }
  }
  ImGui::EndChild();

  ImGui::Spacing();
  const bool can_start =
      !exporting && export_total > 0 && !export_output_directory.empty();
  if (!can_start) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button(export_completed ? "Run Again" : "Start Export",
                    ImVec2(150, 0))) {
    start_export();
  }
  if (!can_start) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  if (ImGui::Button(exporting ? "Close" : "Done", ImVec2(120, 0))) {
    draw_exporting = false;
    if (!exporting) {
      ImGui::CloseCurrentPopup();
    }
  }

  ImGui::EndPopup();
}
