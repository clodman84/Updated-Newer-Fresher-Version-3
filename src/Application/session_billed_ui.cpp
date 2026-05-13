#include "include/session.h"
#include <imgui.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

void Session::render_billed_table() {
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
  for (auto &[student_id, entry] :
       export_manager.bill[image_manager.current_image->filename].entries) {
    if (entry.count < 1) {
      continue;
    }

    ImGui::PushID(student_id.c_str());
    ImGui::TableNextRow();
    if (keyboard_nav_mode == KeyboardNavMode::Billed &&
        visible_index == selected_billed_index) {
      ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                             ImGui::GetColorU32(ImGuiCol_Header));
      if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
        entry.count += 1;
      if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract) ||
          ImGui::IsKeyPressed(ImGuiKey_Backspace))
        entry.count -= 1;
      if (ImGui::IsKeyPressed(ImGuiKey_Delete))
        entry.count = 0;

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
      export_manager.autosave();
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

  if (image_manager.current_image == nullptr) {
    ImGui::TextDisabled("Image not loaded.");
    ImGui::EndChild();
    return;
  }

  if (export_manager.bill[image_manager.current_image->filename]
          .entries.empty()) {
    ImGui::TextDisabled("No billed entries for the current image.");
    ImGui::EndChild();
    return;
  }

  render_billed_table();
  ImGui::EndChild();
}
