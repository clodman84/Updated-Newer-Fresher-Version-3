#include "json.hpp"
#include <SDL3/SDL_log.h>
#include <application.h>
#include <filesystem>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <string>

#include <fstream>
#include <sstream>
#include <stdexcept>

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
  ImGui::SameLine();
  if (ImGui::Button("Same As")) {
    draw_same_as_popup = true;
    ImGui::OpenPopup("Same As Bill");
  }

  const char *current_file =
      manager.current_image ? manager.current_image->filename : nullptr;

  if (ImGui::BeginPopup("Same As Bill")) {
    ImGui::TextUnformatted("Copy billed entries from another image");
    ImGui::Separator();

    const int columns = 5;
    const float cell_w = 150.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float popup_w = columns * cell_w + spacing * (columns - 1);

    ImGui::SetNextWindowSize(
        ImVec2(popup_w + ImGui::GetStyle().WindowPadding.x * 2, 480.0f),
        ImGuiCond_Appearing);

    bool found_source = false;
    int col = 0;
    for (const auto &image_name : manager.get_thumbnail_order()) {
      if (current_file != nullptr && image_name == current_file)
        continue;
      const Thumbnail_T *thumb = manager.get_thumbnail(image_name);
      if (thumb == nullptr || thumb->texture == nullptr)
        continue;

      found_source = true;
      if (col % columns != 0)
        ImGui::SameLine(0.0f, spacing);

      ImGui::PushID(image_name.c_str());
      ImGui::BeginGroup();

      const int frame_number = manager.get_image_index(image_name) + 1;
      if (frame_number > 0)
        ImGui::Text("Frame %d", frame_number);

      const float aspect = (float)thumb->height / (float)thumb->width;
      if (ImGui::ImageButton("##thumb", thumb->texture,
                             ImVec2(cell_w, cell_w * aspect))) {
        append_bill_from_image(image_name);
        draw_same_as_popup = false;
        ImGui::CloseCurrentPopup();
      }

      ImGui::TextUnformatted(
          std::filesystem::path(image_name).filename().string().c_str());
      ImGui::EndGroup();
      ImGui::PopID();
      col++;
    }

    if (!found_source)
      ImGui::TextDisabled("No other images are available.");

    ImGui::Separator();
    if (ImGui::Button("Close")) {
      draw_same_as_popup = false;
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  } else if (draw_same_as_popup && !ImGui::IsPopupOpen("Same As Bill")) {
    draw_same_as_popup = false;
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
  autosave();
}

void Session::autosave() {
  std::filesystem::path filepath = path / "save.json";
  // this has got to be the best library I have ever used in my entire life
  nlohmann::json serialised = bill;
  // WHAT?? THAT'S IT??
  std::ofstream file(filepath, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file");
  }
  file << serialised.dump(4);
  if (!file.good()) {
    throw std::runtime_error("Error writing to file");
  }
  file.close();
}

void Session::append_bill_from_image(
    const std::filesystem::path &source_image) {
  if (!manager.current_image || manager.current_image->filename == nullptr) {
    return;
  }

  const std::filesystem::path current_file = manager.current_image->filename;
  auto source_it = bill.find(source_image);
  if (source_it == bill.end()) {
    return;
  }

  auto &current_bill = bill[current_file];
  for (const auto &[student_id, source_entry] : source_it->second) {
    BillEntry &entry = current_bill[student_id];
    entry.name = source_entry.name;
    entry.count += source_entry.count;
  }
  autosave();
}

Session::~Session() {
  if (export_worker.joinable()) {
    export_worker.join();
  }
}

void Session::open_export_modal() {
  if (!exporting) {
    prepare_export_queue();
  }
  draw_exporting = true;
}

void Session::prepare_export_queue() {
  pending.clear();
  export_font_data.clear();
  if (export_output_directory.empty()) {
    export_output_directory =
        (std::filesystem::path("./Data/") / path.filename()).string();
  }

  size_t total_items = 0;
  for (const auto &image : bill) {
    for (const auto &student_id_bill_pairs : image.second) {
      total_items += std::max(student_id_bill_pairs.second.count, 0);
    }
  }
  pending.reserve(total_items);

  std::string roll = path.filename();
  for (const auto &image : bill) {
    for (const auto &student_id_bill_pairs : image.second) {
      if (student_id_bill_pairs.second.count < 1) {
        continue;
      }
      ExportInfo info =
          database->get_export_information_from_id(student_id_bill_pairs.first);
      std::string watermark = info.bhawan + " " + info.roomno;
      for (int i = 1; i <= student_id_bill_pairs.second.count; i++) {
        std::string filename = roll + "_" + image.first.stem().string() + "_" +
                               info.bhawan + "_" + std::to_string(i) + "_" +
                               info.roomno + "_" + student_id_bill_pairs.first +
                               ".jpg";
        std::filesystem::path destination =
            std::filesystem::path(export_output_directory) / filename;
        pending.push_back({image.first, destination, watermark,
                           image.first.filename().string()});
      }
    }
  }

  export_total = static_cast<int>(pending.size());
  export_progress = 0;
  export_completed = false;
  export_active_items.clear();

  std::ostringstream status;
  status << "Ready: " << pending.size() << " image";
  if (pending.size() != 1) {
    status << 's';
  }
  export_status_message = status.str();
}

void Session::start_export() {
  if (exporting || pending.empty()) {
    return;
  }
  if (export_worker.joinable()) {
    export_worker.join();
  }

  export_font_data.clear();
  if (export_apply_watermark) {
    std::ifstream font_file("./Data/Quantico-Regular.ttf", std::ios::binary);
    export_font_data.assign(std::istreambuf_iterator<char>(font_file), {});
    if (export_font_data.empty()) {
      export_status_message = "Font file missing: ./Data/Quantico-Regular.ttf";
      return;
    }
  }

  std::filesystem::create_directories(export_output_directory);

  export_progress = 0;
  export_completed = false;
  exporting = true;

  {
    std::lock_guard<std::mutex> lock(export_status_mutex);
    export_active_items.assign(1, "Waiting to start");
  }

  std::ostringstream status;
  status << "Writing to " << export_output_directory;
  export_status_message = status.str();
  export_worker = std::thread([this]() { export_images(); });
}

void Session::finish_export_if_ready() {
  if (!exporting || export_progress.load() < export_total) {
    return;
  }

  exporting = false;
  export_completed = true;
  if (export_worker.joinable()) {
    export_worker.join();
  }

  std::lock_guard<std::mutex> lock(export_status_mutex);
  export_active_items.clear();

  std::ostringstream status;
  status << "Finished: " << export_total << " image";
  if (export_total != 1) {
    status << 's';
  }
  status << " written to " << export_output_directory;
  export_status_message = status.str();
}

void Session::draw_export_modal() {
  finish_export_if_ready();

  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
  ImGui::OpenPopup("Export Roll");

  if (ImGui::BeginPopupModal("Export Roll", NULL,
                             ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextColored(ImVec4(0.93f, 0.73f, 0.24f, 1.0f),
                       "Export billed images");
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
                      ImVec2(160, 0))) {
      start_export();
    }
    if (!can_start) {
      ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(160, 0))) {
      draw_exporting = false;
      ImGui::CloseCurrentPopup();
    }

    if (export_total == 0 && !exporting) {
      ImGui::Spacing();
      ImGui::TextDisabled("Nothing to export yet.");
    }

    ImGui::EndPopup();
  }
}
