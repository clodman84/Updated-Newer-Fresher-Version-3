#include "include/session.h"
#include <imgui.h>

void Session::render_carousel(float carousel_height) {
  ImGui::BeginChild("Carousel", ImVec2(0, carousel_height),
                    ImGuiChildFlags_Borders);

  auto io = ImGui::GetIO();
  if (ImGui::IsWindowHovered()) {
    ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseWheel * 50.0f);
  }

  for (int i = 0; i < (int)image_manager.size; i++) {

    const auto &image = image_manager.image_order[i];
    const auto name = image.filename;

    ImGui::PushID(name.c_str());
    bool is_selected = selection_storage.contains(name);
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
            current_image = image_manager.load_image();
            reset_view_to_image();
          }
          last_clicked_index = i;
        },
        is_selected, (i == last_drawn_index), is_context_menu_open);
    ImGui::EndGroup();

    // Right-click context menu for Same As
    if (ImGui::BeginPopupContextItem("ThumbnailContextMenu")) {
      if (!is_selected) {
        selection_storage.clear();
        selection_storage.emplace(name);
      }
      if (ImGui::MenuItem("Same As")) {
        const auto source_it = export_manager.bill.find(name);
        if (selection_storage.size() == 1) {
          selection_storage.emplace(image_manager.current_image_path());
        }
        for (auto &image : selection_storage) {
          if (name != image) {
            export_manager.same_as(name, image);
          }
        }
        selection_storage.clear();
      }
      ImGui::EndPopup();
    }

    if (name == image_manager.current_image_path() &&
        image_manager.index != last_drawn_index) {
      ImGui::SetScrollHereX(0.5f);
    }

    ImGui::PopID();
    ImGui::SameLine();
  }

  ImGui::EndChild();
}
