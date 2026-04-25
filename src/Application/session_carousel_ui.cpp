#include "include/session.h"
#include <algorithm>
#include <imgui.h>

void Session::render_carousel(float carousel_height) {
  ImGui::BeginChild("Carousel", ImVec2(0, carousel_height),
                    ImGuiChildFlags_Borders);

  auto io = ImGui::GetIO();
  if (ImGui::IsWindowHovered()) {
    ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseWheel * 50.0f);
  }

  int currently_visible_start = -1;
  int currently_visible_end = -1;

  for (int i = 0; i < (int)image_manager.size; i++) {

    const auto &image = image_manager.image_order[i];
    const auto current_image_filename = image.filename;

    ImGui::PushID(current_image_filename.c_str());
    bool is_selected = selection_storage.contains(current_image_filename);
    bool is_context_menu_open = ImGui::IsPopupOpen("ThumbnailContextMenu");

    ImGui::BeginGroup();
    ImGui::Text("Frame: %d", i + 1);
    image.render_thumbnail(
        200,
        [this, i](const std::string &n) {
          auto &io = ImGui::GetIO();
          if (io.KeyShift && last_clicked_index != -1) {
            int start = std::min(i, last_clicked_index);
            int end = std::max(i, last_clicked_index);
            if (!io.KeyCtrl)
              selection_storage.clear();
            for (int j = start; j <= end; j++) {
              selection_storage.emplace(image_manager.image_order[j].filename);
            }
          } else if (io.KeyCtrl) {
            if (!selection_storage.contains(n))
              selection_storage.emplace(n);
            else
              selection_storage.erase(n);
          } else {
            selection_storage.clear();
            image_manager.index = i;
            image_manager.load_image();
            reset_view_to_image();
          }
          last_clicked_index = i;
        },
        is_selected, (i == last_drawn_index), is_context_menu_open);
    ImGui::EndGroup();

    if (ImGui::IsItemVisible()) {
      if (currently_visible_start == -1)
        currently_visible_start = i;
      currently_visible_end = i;
    };

    // Right-click context menu for Same As
    if (ImGui::BeginPopupContextItem("ThumbnailContextMenu")) {
      if (!is_selected) {
        selection_storage.clear();
        selection_storage.emplace(current_image_filename);
      }
      if (ImGui::MenuItem("Same As")) {
        const auto source_it = export_manager.bill.find(current_image_filename);
        if (selection_storage.size() == 1) {
          selection_storage.emplace(image_manager.current_image_path());
        }
        for (auto &selected_image_filenames : selection_storage) {
          if (current_image_filename != selected_image_filenames) {
            export_manager.same_as(current_image_filename,
                                   selected_image_filenames);
          }
        }
        selection_storage.clear();
      }
      ImGui::EndPopup();
    }

    if (current_image_filename == image_manager.current_image_path() &&
        image_manager.index != last_drawn_index) {
      ImGui::SetScrollHereX(0.5f);
    }

    ImGui::PopID();
    ImGui::SameLine();
  }
  ImGui::EndChild();
  currently_visible_start = std::max(0, currently_visible_start - 1);
  currently_visible_end =
      std::min(image_manager.size - 1, currently_visible_end + 1);
  if (currently_visible_start != visible_start ||
      currently_visible_end != visible_end) {
    visible_start = currently_visible_start;
    visible_end = currently_visible_end;
    printf("Updated Visibility: %d -> %d\n", currently_visible_start,
           currently_visible_end);
    image_manager.load_thumbnail_range(visible_start, visible_end);
    image_manager.schedule_thumbnail_cleanup(visible_start, visible_end);
  }
}
