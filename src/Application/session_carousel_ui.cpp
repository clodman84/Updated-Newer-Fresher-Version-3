#include "include/IconsFontAwesome6.h"
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
    ImGui::Text("Frame %d", i + 1);
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
            image_manager.load_image(i);
            reset_view_to_image();
          }
          last_clicked_index = i;
        },
        is_selected, (i == last_drawn_index), is_context_menu_open);

    {
      ImVec2 thumb_min = ImGui::GetItemRectMin();
      ImVec2 thumb_max = ImGui::GetItemRectMax();

      const float pad = 4.0f;
      const float spacing = 4.0f;
      ImVec2 bookmark_size = ImGui::CalcTextSize(ICON_FA_BOOKMARK);
      ImVec2 circle_size = ImGui::CalcTextSize(ICON_FA_CIRCLE);
      ImVec2 btn_size =
          ImVec2(bookmark_size.x + pad * 2, bookmark_size.y + pad * 2);
      ImVec2 bookmark_pos(thumb_max.x - btn_size.x + 6,
                          thumb_min.y - btn_size.y);
      bool bookmarked =
          export_manager.bill[current_image_filename].attributes.bookmark;
      ImGui::SetCursorScreenPos(bookmark_pos);
      ImGui::SetNextItemAllowOverlap();
      ImGui::PushID("bookmark");
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.4f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0.6f));
      ImGui::PushStyleColor(ImGuiCol_Text,
                            bookmarked ? ImVec4(1.0f, 0.84f, 0.0f, 1.0f)
                                       : ImVec4(1.0f, 1.0f, 1.0f, 0.6f));
      if (ImGui::Button(ICON_FA_BOOKMARK, btn_size)) {
        export_manager.bill[current_image_filename].attributes.bookmark =
            !bookmarked;
        export_manager.autosave();
      }
      bool has_entries =
          !export_manager.bill[current_image_filename].entries.empty();
      ImGui::SetCursorScreenPos(
          ImVec2(bookmark_pos.x - circle_size.x, bookmark_pos.y + 4));
      ImGui::Text(has_entries ? ICON_FA_CIRCLE_DOT : ICON_FA_CIRCLE);
      ImGui::PopStyleColor(4);
      ImGui::PopID();
    }

    ImGui::EndGroup();
    // Right-click context menu for Same As
    if (ImGui::BeginPopupContextItem("ThumbnailContextMenu")) {
      if (!is_selected) {
        selection_storage.clear();
        selection_storage.emplace(current_image_filename);
      }
      if (ImGui::MenuItem(ICON_FA_PAINT_ROLLER "  Same As")) {
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

    if (ImGui::IsItemVisible()) {
      if (currently_visible_start == -1)
        currently_visible_start = i;
      currently_visible_end = i;
    };

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
    image_manager.load_thumbnail_range(visible_start, visible_end);
    image_manager.schedule_thumbnail_cleanup(visible_start, visible_end);
  }
}
