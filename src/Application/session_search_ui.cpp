#include "include/IconsFontAwesome6.h"
#include "include/session.h"
#include <imgui.h>
#include <imgui_stdlib.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

void Session::render_searcher() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::render_searcher");
#endif
  ImGui::BeginChild("Search Window", {0.0f, 650.0f}, ImGuiChildFlags_ResizeY);

  if (focus_search_on_next_frame) {
    ImGui::SetKeyboardFocusHere();
    focus_search_on_next_frame = false;
  }

  if (ImGui::InputTextWithHint("##search_query", ICON_FA_MAGNIFYING_GLASS,
                               &search_query)) {
    search_results = database.evaluate(search_query);
  }
  sync_search_selection_bounds();

  ImGui::SameLine();
  if (ImGui::ArrowButton("Previous", ImGuiDir_Left)) {
    image_manager.load_previous();
  }
  ImGui::SameLine(0.0f, 0.0f);
  if (ImGui::ArrowButton("Next", ImGuiDir_Right)) {
    image_manager.load_next();
  }
  ImGui::SameLine();

  if (ImGui::Button(ICON_FA_COPY)) {
    if (image_manager.index > 0 && image_manager.index <= image_manager.size) {
      export_manager.same_as(
          image_manager.image_path_from_index(image_manager.index - 1),
          image_manager.image_path_from_index(image_manager.index));
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Same As");
  }

  render_search_results_table();
  ImGui::EndChild();
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
          export_manager.increment_for_id(
              line[column], line[column + 1],
              image_manager.current_image->filename);
        }
      } else {
        ImGui::TextUnformatted(line[column].c_str());
      }
    }
  }

  ImGui::EndTable();
}
