#include <SDL3/SDL_log.h>
#include <application.h>
#include <imgui.h>
#include <json.hpp>
#include <misc/cpp/imgui_stdlib.h>
#include <string>

void Session::handle_keyboard_nav() {

  if (!ImGui::GetIO().WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
      manager.load_previous();
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
      manager.load_next();
  }

  if (search_results.empty()) {
    selected_search_index = 0;
  } else {
    selected_search_index =
        std::clamp(selected_search_index, 0, (int)search_results.size() - 1);
  }

  int billed_entry_count = 0;
  const char *current_file =
      manager.current_image ? manager.current_image->filename : nullptr;
  if (current_file != nullptr) {
    auto it = bill.find(current_file);
    if (it != bill.end()) {
      for (const auto &line : it->second) {
        if (line.second.count > 0)
          billed_entry_count++;
      }
    }
  }
  if (billed_entry_count == 0) {
    selected_billed_index = 0;
  } else {
    selected_billed_index =
        std::clamp(selected_billed_index, 0, billed_entry_count - 1);
  }

  if (keyboard_nav_mode == KeyboardNavMode::Search) {
    if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !bill[current_file].empty()) {
      keyboard_nav_mode = KeyboardNavMode::Billed;
      focus_billed_on_next_frame = true;
      return;
    }

    if (!search_results.empty()) {
      if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        selected_search_index = std::max(0, selected_search_index - 1);
      if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        selected_search_index =
            std::min((int)search_results.size() - 1, selected_search_index + 1);
      if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        const auto &line = search_results[selected_search_index];
        increment_for_id(line[0], line[1]);
      }
    }
  } else {
    if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
      keyboard_nav_mode = KeyboardNavMode::Search;
      focus_search_on_next_frame = true;
      return;
    }
    if (current_file != nullptr && billed_entry_count > 0) {
      if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        selected_billed_index = std::max(0, selected_billed_index - 1);
      if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        selected_billed_index =
            std::min(billed_entry_count - 1, selected_billed_index + 1);

      if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        int visible_index = 0;
        for (auto &line : bill.at(current_file)) {
          if (line.second.count < 1)
            continue;
          if (visible_index == selected_billed_index) {
            line.second.count += 1;
            autosave();
            break;
          }
          visible_index++;
        }
      }
    }
  }
}

void Session::render_searcher() {
  ImGui::BeginChild("Search Window", {0.0f, 650.0f}, ImGuiChildFlags_ResizeY);

  if (focus_search_on_next_frame) {
    focus_search_on_next_frame = false;
  }

  if (ImGui::InputTextWithHint("##", "Search", &search_query))
    evaluate();

  if (search_results.empty())
    selected_search_index = 0;
  else
    selected_search_index =
        std::clamp(selected_search_index, 0, (int)search_results.size() - 1);

  ImGui::SameLine();
  if (ImGui::ArrowButton("Previous", ImGuiDir_Left)) {
    this->manager.load_previous();
  }
  ImGui::SameLine(0.0f, 0.0f);
  if (ImGui::ArrowButton("Next", ImGuiDir_Right)) {
    this->manager.load_next();
  }

  if (ImGui::BeginTable(
          "##", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
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
      for (int i = 0; i < 4; i++) {
        ImGui::TableNextColumn();
        if (i == 0) {
          if (ImGui::Button(line[i].c_str()))
            increment_for_id(line[i], line[i + 1]);
        } else
          ImGui::TextUnformatted(line[i].c_str());
      }
    }
    ImGui::EndTable();
  }
  ImGui::EndChild();
}

void Session::render_billed() {
  ImGui::BeginChild("Billed Window", {0.0f, 0.0f});

  if (focus_billed_on_next_frame) {
    focus_billed_on_next_frame = false;
  }

  const char *current_file = this->manager.current_image->filename;
  if (bill.count(current_file) == 0) {
    ImGui::EndChild();
    return;
  }

  if (ImGui::BeginTable(
          "##", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableHeadersRow();

    int visible_index = 0;
    for (auto &line : bill[current_file]) {
      if (line.second.count < 1)
        continue;
      ImGui::PushID(line.first.c_str());
      ImGui::TableNextRow();
      if ((keyboard_nav_mode == KeyboardNavMode::Billed) &&
          (visible_index == selected_billed_index)) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                               ImGui::GetColorU32(ImGuiCol_Header));
        ImGui::SetScrollHereY();
      }
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(line.first.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(line.second.name.c_str());
      ImGui::TableNextColumn();
      ImGui::PushItemWidth(100.0f);
      if (ImGui::InputInt("##xx", &line.second.count))
        autosave();
      ImGui::PopItemWidth();
      ImGui::PopID();
      visible_index++;
    }
    ImGui::EndTable();
  }
  ImGui::EndChild();
}

void Session::increment_for_id(std::string id, std::string name) {
  const char *current_file = this->manager.current_image->filename;
  bill[current_file][id].name = name;
  bill[current_file][id].count += 1;
}

void Session::autosave() {}
