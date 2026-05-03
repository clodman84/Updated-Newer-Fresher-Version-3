#pragma once

#include "google_drive.h"
#include <imgui.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Usage:
//
//   // In your button/menu:
//   if (ImGui::MenuItem("Drive Link"))
//       DriveBrowserUI::open();
//
//   // Once per frame in your main render loop, unconditionally:
//   DriveBrowserUI::draw();
//
// ─────────────────────────────────────────────────────────────────────────────

namespace DriveBrowserUI {

namespace detail {

enum class Phase { Setup, Loading, Results, Error };

inline bool is_open = false;
inline Phase phase = Phase::Setup;
inline std::string status_msg;

inline char creds_path[512] = "";
inline char folder_id[256] = "";

inline std::vector<gdrive::DriveItem> items;

inline std::atomic<bool> result_ready{false};
inline std::mutex result_mutex;
inline std::vector<gdrive::DriveItem> pending_items;
inline std::string pending_error;

inline void begin_load() {
  phase = Phase::Loading;
  result_ready = false;
  pending_items.clear();
  pending_error.clear();

  std::string cp = creds_path;
  std::string fi = folder_id;

  std::thread([cp, fi]() {
    try {
      auto creds = gdrive::ServiceAccountCredentials::from_file(cp);
      gdrive::DriveClient client(std::move(creds));
      client.connect();
      auto result = client.get_folder_contents(fi);
      std::lock_guard<std::mutex> lock(result_mutex);
      pending_items = std::move(result);
    } catch (const std::exception &e) {
      std::lock_guard<std::mutex> lock(result_mutex);
      pending_error = e.what();
    }
    result_ready = true;
  }).detach();
}

inline void collect() {
  if (phase != Phase::Loading || !result_ready.load())
    return;
  std::lock_guard<std::mutex> lock(result_mutex);
  if (!pending_error.empty()) {
    status_msg = pending_error;
    phase = Phase::Error;
  } else {
    items = std::move(pending_items);
    phase = Phase::Results;
  }
  result_ready = false;
}

} // namespace detail

// ── Public API
// ────────────────────────────────────────────────────────────────

inline void open() {
  detail::is_open = true;
  detail::phase = detail::Phase::Setup;
  detail::status_msg = "";
  detail::items.clear();
}

inline void draw() {
  using namespace detail;

  if (!is_open)
    return;
  collect();

  ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                                 ImGui::GetIO().DisplaySize.y * 0.5f),
                          ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  if (!ImGui::Begin("Drive Browser", &is_open)) {
    ImGui::End();
    return;
  }

  // ── Setup ──────────────────────────────────────────────────────────────
  if (phase == Phase::Setup) {
    ImGui::TextDisabled("Service account credentials");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##creds", "/path/to/service-account.json",
                             creds_path, sizeof(creds_path));
    ImGui::Spacing();

    ImGui::TextDisabled("Folder ID");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##folder", "1aBcDeFgHiJkLmNoPqRsTuVwXyZ",
                             folder_id, sizeof(folder_id));
    ImGui::Spacing();

    const bool ready = creds_path[0] != '\0' && folder_id[0] != '\0';
    if (!ready)
      ImGui::BeginDisabled();
    if (ImGui::Button("Load", ImVec2(80, 0)))
      begin_load();
    if (!ready)
      ImGui::EndDisabled();

    if (!status_msg.empty()) {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", status_msg.c_str());
    }
  }

  // ── Loading ────────────────────────────────────────────────────────────
  else if (phase == Phase::Loading) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 text_size = ImGui::CalcTextSize("Loading...");
    ImGui::SetCursorPos(ImVec2((avail.x - text_size.x) * 0.5f, avail.y * 0.4f));
    ImGui::Text("Loading...");
  }

  // ── Error ──────────────────────────────────────────────────────────────
  else if (phase == Phase::Error) {
    ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Error");
    ImGui::Spacing();
    ImGui::TextWrapped("%s", status_msg.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Back")) {
      phase = Phase::Setup;
      status_msg = "";
    }
  }

  // ── Results ────────────────────────────────────────────────────────────
  else if (phase == Phase::Results) {
    if (ImGui::Button("Back")) {
      phase = Phase::Setup;
      items.clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu items  —  %s", items.size(), folder_id);
    ImGui::Separator();

    if (items.empty()) {
      ImGui::TextDisabled("Folder is empty.");
    } else {
      constexpr ImGuiTableFlags tf =
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

      if (ImGui::BeginTable("##items", 3, tf)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 55.f);
        ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed,
                                140.f);
        ImGui::TableHeadersRow();

        for (const auto &item : items) {
          ImGui::TableNextRow();

          ImGui::TableSetColumnIndex(0);
          const char *icon =
              item.type == gdrive::FileType::Folder ? "[D] " : "[F] ";
          ImGui::Text("%s%s", icon, item.name.c_str());
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("ID: %s", item.id.c_str());

          ImGui::TableSetColumnIndex(1);
          ImGui::TextDisabled(item.type == gdrive::FileType::Folder ? "Folder"
                                                                    : "File");

          ImGui::TableSetColumnIndex(2);
          std::string mod = item.modified_time.size() >= 16
                                ? item.modified_time.substr(0, 16)
                                : item.modified_time;
          if (mod.size() > 10 && mod[10] == 'T')
            mod[10] = ' ';
          ImGui::TextDisabled("%s", mod.c_str());
        }
        ImGui::EndTable();
      }
    }
  }

  ImGui::End();
}

} // namespace DriveBrowserUI
