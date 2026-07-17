#include "include/google_drive_browser.h"
#include "include/IconsFontAwesome6.h"
#include "include/dino_jumpy.h"
#include "include/imgui_custom.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <memory>

GoogleDriveBrowser::GoogleDriveBrowser(std::shared_ptr<DriveClient> client,
                                       SDL_Window *window)
    : client_(client), window_(window) {
  id_buf_[0] = '\0';
}

void GoogleDriveBrowser::render() {
  poll_fetch();

  if (dl_state_ != DownloadState::Idle) {
    draw_download_overlay();
    return;
  }

  draw_toolbar();
  draw_error_bar();
  draw_file_table();
}

bool GoogleDriveBrowser::is_busy() const {
  return fetching_ || dl_state_ == DownloadState::Active;
}

void GoogleDriveBrowser::draw_toolbar() {
  // Back button
  ImGui::BeginDisabled(nav_history_.empty());
  if (ImGui::Button(ICON_FA_ARROW_LEFT "##back")) {
    std::string prev = nav_history_.back();
    nav_history_.pop_back();
    navigate_to(prev, /*push_history=*/false);
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Back");

  ImGui::SameLine(0, 0.2);

  ImGui::BeginDisabled(fetching_ || current_folder_id_.empty());
  if (ImGui::Button(ICON_FA_ROTATE "##refresh"))
    navigate_to(current_folder_id_, /*push_history=*/false);
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Refresh");

  ImGui::SameLine();

  bool enter = ImGui::InputTextWithHint(
      "##folderid", ICON_FA_FOLDER " Folder ID…", id_buf_, sizeof(id_buf_),
      ImGuiInputTextFlags_EnterReturnsTrue);

  ImGui::SameLine();

  if (ImGui::Button(ICON_FA_SATELLITE_DISH) || enter) {
    std::string target(id_buf_);
    if (!target.empty()) {
      if (target != current_folder_id_)
        navigate_to(target);
      else
        navigate_to(target, false); // re-fetch same folder
    }
  }

  // Inline spinner while loading
  if (fetching_) {
    ImGui::SameLine();
    draw_spinner();
  }
}

void GoogleDriveBrowser::draw_error_bar() {
  if (error_.empty())
    return;
  ImGui::Spacing();
  ImGui::TextColored({1.f, 0.35f, 0.35f, 1.f}, ICON_FA_CIRCLE_EXCLAMATION " %s",
                     error_.c_str());
}

void GoogleDriveBrowser::draw_file_table() {
  ImGui::Separator();
  if (current_folder_id_.empty() && current_items_.empty()) {
    ImGui::Spacing();
    ImGui::TextDisabled("Enter a folder ID above to start browsing.");
    // dino_jumpy.render_frame();
    return;
  }

  if (fetching_) {
    return;
  }

  if (current_items_.empty()) {
    ImGui::Spacing();
    ImGui::TextDisabled(ICON_FA_GHOST "  Nothing to see here");
    return;
  }

  constexpr ImGuiTableFlags kTableFlags =
      ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuter |
      ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
      ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

  if (!ImGui::BeginTable("##driveitems", 3, kTableFlags))
    return;

  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.f);
  ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 160.f);
  ImGui::TableHeadersRow();

  for (const auto &item : current_items_) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    std::string label =
        std::string(icon_for(item)) + "  " + item.name + "##" + item.id;

    bool clicked = ImGui::Selectable(label.c_str(), false,
                                     ImGuiSelectableFlags_SpanAllColumns |
                                         ImGuiSelectableFlags_AllowDoubleClick);

    if (ImGui::BeginPopupContextItem(("ctx##" + item.id).c_str())) {
      if (ImGui::MenuItem(ICON_FA_DOWNLOAD " Download")) {
        dl_item_ = item;
        dl_state_ = DownloadState::WaitingForPath;
        SDL_ShowOpenFolderDialog(on_folder_picked, this, window_, nullptr,
                                 false);
      }
      ImGui::EndPopup();
    }

    if (clicked && ImGui::IsMouseDoubleClicked(0) &&
        item.type == FileType::Folder) {
      navigate_to(item.id);
    }

    ImGui::TableSetColumnIndex(1);
    if (item.type != FileType::Folder)
      ImGui::TextDisabled("%s", friendly_size(item.size_bytes).c_str());

    ImGui::TableSetColumnIndex(2);
    ImGui::TextDisabled("%s", friendly_time(item.modified_time).c_str());
  }

  ImGui::EndTable();
}

void GoogleDriveBrowser::draw_download_overlay() {

  if (dl_state_ == DownloadState::WaitingForPath) {
    ImGui::TextDisabled(ICON_FA_FOLDER_OPEN "  Waiting for folder selection…");
    ImGui::Spacing();
    if (ImGui::Button("Cancel")) {
      dl_state_ = DownloadState::Idle;
    }
    return;
  }

  {
    std::lock_guard lock(dl_msg_mutex_);
    ImGui::TextWrapped("%s", dl_message_.c_str());
  }

  float progress = dl_progress_.load();
  char overlay[32];

  if (dl_state_ == DownloadState::Done) {
    if (ImGui::Button(ICON_FA_ARROW_LEFT)) {
      dl_state_ = DownloadState::Idle;
      error_.clear();
    }
    ImGui::SameLine();
  }

  if (dl_state_ == DownloadState::Done) {
    snprintf(overlay, sizeof(overlay), "Done");
  } else if (progress > 0.f) {
    snprintf(overlay, sizeof(overlay), "%d%%",
             static_cast<int>(progress * 100.f));
  } else {
    snprintf(overlay, sizeof(overlay), "…");
  }
  ImGui::ProgressBar(progress, {-1.f, 0.f}, overlay);

  if (dl_state_ == DownloadState::Active && dl_future_.valid() &&
      dl_future_.wait_for(std::chrono::seconds(0)) ==
          std::future_status::ready) {
    try {
      dl_future_.get();
      set_message(ICON_FA_CIRCLE_CHECK "  Download complete: " + dl_item_.name);
    } catch (const std::exception &e) {
      set_message(std::string(ICON_FA_CIRCLE_EXCLAMATION "  Failed: ") +
                  e.what());
    }
    dl_state_ = DownloadState::Done;
    dl_progress_ = 1.f;
  }

  ImGui::Separator();
  // dino_jumpy.render_frame();
}

void GoogleDriveBrowser::navigate_to(const std::string &folder_id,
                                     bool push_history) {
  if (fetching_)
    return;

  if (push_history && !current_folder_id_.empty())
    nav_history_.push_back(current_folder_id_);

  current_folder_id_ = folder_id;
  strncpy(id_buf_, folder_id.c_str(), sizeof(id_buf_) - 1);
  id_buf_[sizeof(id_buf_) - 1] = '\0';

  error_.clear();
  fetching_ = true;

  fetch_future_ = std::async(std::launch::async, [this, folder_id] {
    return client_->get_folder_contents(folder_id);
  });
}

void GoogleDriveBrowser::poll_fetch() {
  if (!fetching_ || !fetch_future_.valid())
    return;
  if (fetch_future_.wait_for(std::chrono::seconds(0)) !=
      std::future_status::ready)
    return;

  try {
    current_items_ = fetch_future_.get();
  } catch (const std::exception &e) {
    error_ = e.what();
    current_items_.clear();
  }
  fetching_ = false;
}

void GoogleDriveBrowser::begin_download(const DriveItem &item,
                                        const std::string &dest_path) {
  dl_item_ = item;
  dl_state_ = DownloadState::Active;
  dl_progress_ = 0.f;
  set_message("Starting: " + item.name);

  dl_future_ = std::async(std::launch::async, [this, dest_path] {
    std::filesystem::path dest(dest_path);

    if (dl_item_.type == FileType::File) {
      client_->download_file(
          dl_item_, dest, [this](long long done, long long total) {
            if (total > 0)
              dl_progress_ = static_cast<float>(done) / total;
          });
    } else {
      client_->download_folder(
          dl_item_, dest, [this](int done, int total, const DriveItem &curr) {
            std::lock_guard lock(dl_msg_mutex_);
            if (total == 0) {
              dl_message_ = "Mapping: " + curr.name;
            } else {
              dl_message_ = "(" + std::to_string(done) + "/" +
                            std::to_string(total) + ")  " + curr.name;
              dl_progress_ = static_cast<float>(done) / total;
            }
          });
    }
  });

  dino_jumpy = Game();
}

void GoogleDriveBrowser::set_message(const std::string &msg) {
  std::lock_guard lock(dl_msg_mutex_);
  dl_message_ = msg;
}

void SDLCALL GoogleDriveBrowser::on_folder_picked(void *userdata,
                                                  const char *const *filelist,
                                                  int /*filter*/) {
  auto *self = static_cast<GoogleDriveBrowser *>(userdata);
  if (filelist == nullptr || *filelist == nullptr) {
    // User cancelled
    self->dl_state_ = DownloadState::Idle;
    return;
  }
  self->begin_download(self->dl_item_, *filelist);
}

const char *GoogleDriveBrowser::icon_for(const DriveItem &item) {
  if (item.type == FileType::Folder)
    return ICON_FA_FOLDER;
  if (item.mime_type == "image/jpeg" || item.mime_type == "image/png" ||
      item.mime_type == "image/gif")
    return ICON_FA_IMAGE;
  if (item.mime_type == "text/csv")
    return ICON_FA_FILE_CSV;
  if (item.mime_type == "text/plain")
    return ICON_FA_FILE_LINES;
  if (item.mime_type == "application/pdf")
    return ICON_FA_FILE_PDF;
  if (item.mime_type == "application/zip" ||
      item.mime_type == "application/x-tar")
    return ICON_FA_FILE_ZIPPER;
  if (item.mime_type.find("audio") != std::string::npos)
    return ICON_FA_FILE_AUDIO;
  if (item.mime_type.find("video") != std::string::npos)
    return ICON_FA_FILE_VIDEO;
  return ICON_FA_FILE;
}

std::string GoogleDriveBrowser::friendly_size(long long bytes) {
  if (bytes <= 0)
    return "--";
  const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  double val = static_cast<double>(bytes);
  int i = 0;
  while (val >= 1024.0 && i < 4) {
    val /= 1024.0;
    ++i;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), i == 0 ? "%.0f %s" : "%.1f %s", val, units[i]);
  return buf;
}

std::string GoogleDriveBrowser::friendly_time(const std::string &rfc3339) {
  if (rfc3339.size() < 16)
    return rfc3339;
  return rfc3339.substr(0, 10) + "  " + rfc3339.substr(11, 5);
}
