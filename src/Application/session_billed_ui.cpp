#include "include/IconsFontAwesome6.h"
#include "include/session.h"
#include <imgui.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

void Session::render_billed_table() {
  if (export_manager.request_autosave)
    export_manager.autosave();
  float bottom_bar_height = ImGui::GetFrameHeightWithSpacing() +
                            ImGui::GetStyle().ItemSpacing.y * 2 + 2.0f;
  int sum = 0;
  if (ImGui::BeginChild("##billed_table_scroll", ImVec2(0, -bottom_bar_height),
                        false, ImGuiWindowFlags_None)) {
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
        if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
          entry.count += 1;
          export_manager.request_autosave = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract) ||
            ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
          entry.count -= 1;
          export_manager.request_autosave = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
          entry.count = 0;
          export_manager.request_autosave = true;
        }

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
        export_manager.request_autosave = true;
      }
      sum += count;
      ImGui::PopItemWidth();
      ImGui::PopID();
      ++visible_index;
    }

    ImGui::EndTable();
  }
  ImGui::EndChild();

  ImGui::Separator();
  ImGui::Spacing();

  float available = ImGui::GetContentRegionAvail().x;
  ImGui::Text("%d people billed", sum);
  ImGui::SameLine();

  bool is_finalised = export_manager.bill[image_manager.current_image->filename]
                          .attributes.finalised;
  if (is_finalised) {
    ImGui::BeginDisabled();
  }

  float button_width = ImGui::CalcTextSize(ICON_FA_CIRCLE_CHECK).x +
                       ImGui::GetStyle().FramePadding.x * 2;
  ImGui::SetCursorPosX(available - button_width);

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.52f, 0.30f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.22f, 0.62f, 0.36f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(0.14f, 0.42f, 0.24f, 1.0f));

  if (ImGui::Button(ICON_FA_CIRCLE_CHECK)) {
    auto &attrs =
        export_manager.bill[image_manager.current_image->filename].attributes;
    attrs.finalised = true;
    attrs.bookmark = false;
  }

  ImGui::PopStyleColor(3);

  if (is_finalised) {
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(ICON_FA_LOCK "  Finalised");
  }
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
