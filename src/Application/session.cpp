#include <SDL3/SDL_log.h>
#include <application.h>
#include <imgui.h>
#include <json.hpp>
#include <misc/cpp/imgui_stdlib.h>
#include <string>

void Session::render_searcher() {
  ImGui::BeginChild("Search Window", {0.0f, 650.0f}, ImGuiChildFlags_ResizeY);
  if (ImGui::InputTextWithHint("##", "Search", &search_query))
    this->database->search(ID_SEARCH, search_query, search_results);
  ImGui::SameLine();
  if (ImGui::ArrowButton("Previous", ImGuiDir_Left)) {
    this->manager.loadPrevious();
  }
  ImGui::SameLine(0.0f, 0.0f);
  if (ImGui::ArrowButton("Next", ImGuiDir_Right)) {
    this->manager.loadNext();
  }

  ImGui::BeginTable("##", 4,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit);
  ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 110.0f);
  ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
  ImGui::TableSetupColumn("Bhawan", ImGuiTableColumnFlags_WidthFixed, 50.0f);
  ImGui::TableSetupColumn("Room", ImGuiTableColumnFlags_WidthFixed, 40.0f);

  ImGui::TableHeadersRow();
  for (const auto &line : search_results) {
    ImGui::TableNextRow();
    for (int i = 0; i < 4; i++) {
      ImGui::TableNextColumn();
      if (i == 0) {
        if (ImGui::Button(line[i].c_str()))
          log_id(line[i], line[i + 1]);
      } else
        ImGui::TextUnformatted(line[i].c_str());
    }
  }
  ImGui::EndTable();
  ImGui::EndChild();
}

void Session::render_billed() {
  ImGui::BeginChild("Billed Window", {0.0f, 0.0f});
  const char *current_file = this->manager.current_image->filename;
  if (bill.count(current_file) == 0) {
    ImGui::EndChild();
    return;
  }

  ImGui::BeginTable("##", 3,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit);
  ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 110.0f);
  ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
  ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 50.0f);

  ImGui::TableHeadersRow();

  for (const auto &line : bill[current_file]) {
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(line.first.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(line.second.name.c_str());
    ImGui::TableNextColumn();
    ImGui::Text("%d", line.second.count);
  }

  ImGui::EndTable();
  ImGui::EndChild();
}

void Session::log_id(std::string id, std::string name) {
  const char *current_file = this->manager.current_image->filename;
  bill[current_file][id].name = name;
  bill[current_file][id].count += 1;
}
