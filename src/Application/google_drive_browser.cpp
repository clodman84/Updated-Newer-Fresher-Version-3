#include "include/google_drive_browser.h"
#include "include/IconsFontAwesome6.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <imgui.h>

static void SDLCALL folder_picker_callback(void *userdata,
                                           const char *const *filelist,
                                           int filter) {
  auto *browser = static_cast<GoogleDriveBrowser *>(userdata);
  if (filelist != nullptr && *filelist != nullptr) {
    browser->start_download(*filelist); // Start download at the picked path
  } else {
    browser->cancel_download_state();
  }
}

static const SDL_DialogFileFilter json_filters[] = {{"JSON files", "json"}};

static void SDLCALL import_cred_callback(void *userdata,
                                         const char *const *filelist,
                                         int filter) {
  if (filelist == nullptr || *filelist == nullptr)
    return;

  auto *browser = static_cast<GoogleDriveBrowser *>(userdata);
  std::filesystem::path file_path(*filelist);

  std::ifstream ifs(file_path, std::ios::in | std::ios::binary);
  if (ifs.is_open()) {
    std::string json_content((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());

    // Save to DB and initialize the client
    if (browser->database.save_credentials(json_content)) {
      browser->load_client_from_db();
    }
  }
}

GoogleDriveBrowser::GoogleDriveBrowser(SDL_Window *window) : window_(window) {
  current_folder_id_ = "";
  folder_id_input_[0] = '\0';
  if (database.has_credentials()) {
    load_client_from_db();
  } else {
    show_import_modal_ = true;
  }
}

void GoogleDriveBrowser::load_client_from_db() {
  try {
    std::string json_data = database.get_credentials();
    ServiceAccountCredentials creds =
        ServiceAccountCredentials::from_json(json_data);
    client_ = std::make_unique<DriveClient>(std::move(creds));
    init_failed_ = false;
    show_import_modal_ = false;
  } catch (const std::exception &e) {
    init_failed_ = true;
    error_message_ = e.what();
    show_import_modal_ = true; // Show the modal again if parsing failed
  }
}

void GoogleDriveBrowser::load_folder_async(const std::string &folder_id) {
  if (is_loading_)
    return;

  is_loading_ = true;
  error_message_.clear();
  current_folder_id_ = folder_id;

  strncpy(folder_id_input_, current_folder_id_.c_str(),
          sizeof(folder_id_input_) - 1);
  folder_id_input_[sizeof(folder_id_input_) - 1] = '\0';

  fetch_future_ = std::async(std::launch::async, [this, folder_id]() {
    return client_->get_folder_contents(folder_id);
  });
}

void GoogleDriveBrowser::cancel_download_state() {
  waiting_for_folder_picker_ = false;
}

void GoogleDriveBrowser::start_download(const std::string &dest_folder) {
  waiting_for_folder_picker_ = false;
  is_downloading_ = true;
  download_progress_ = 0.0f;

  {
    std::lock_guard<std::mutex> lock(ui_mutex_);
    download_message_ = "Preparing download: " + item_to_download_.name;
  }

  std::string dest_dir_str = dest_folder;
  download_future_ = std::async(std::launch::async, [this, dest_dir_str]() {
    try {
      std::filesystem::path dest_dir(dest_dir_str);

      if (item_to_download_.type == FileType::File) {
        client_->download_file(item_to_download_, dest_dir,
                               [this](long long done, long long total) {
                                 if (total > 0) {
                                   this->download_progress_ =
                                       static_cast<float>(done) /
                                       static_cast<float>(total);
                                 }
                               });
      } else if (item_to_download_.type == FileType::Folder) {
        client_->download_folder(
            item_to_download_, dest_dir,
            [this](int done, int total, const DriveItem &curr) {
              std::lock_guard<std::mutex> lock(this->ui_mutex_);
              if (total == 0) {
                this->download_message_ =
                    "Mapping directory structure: " + curr.name;
              } else {
                this->download_message_ =
                    "Downloading file (" + std::to_string(done) + "/" +
                    std::to_string(total) + "): " + curr.name;
                this->download_progress_ =
                    static_cast<float>(done) / static_cast<float>(total);
              }
            });
      }

      std::lock_guard<std::mutex> lock(this->ui_mutex_);
      this->download_message_ =
          "Successfully downloaded: " + item_to_download_.name;
      this->download_progress_ = 1.0f;

    } catch (const std::exception &e) {
      std::lock_guard<std::mutex> lock(this->ui_mutex_);
      this->download_message_ = std::string("Download failed: ") + e.what();
      this->download_progress_ = 0.0f;
    }
  });
}

std::string GoogleDriveBrowser::format_size(long long bytes) {
  if (bytes == 0)
    return "--";
  const char *units[] = {"B", "KB", "MB", "GB"};
  int i = 0;
  double dbl_bytes = static_cast<double>(bytes);

  while (dbl_bytes >= 1024.0 && i < 3) {
    dbl_bytes /= 1024.0;
    i++;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f %s", dbl_bytes, units[i]);
  return buf;
}

void GoogleDriveBrowser::render_window(const char *window_title) {

  if (show_import_modal_) {
    if (!ImGui::IsPopupOpen("Import Credentials")) {
      ImGui::OpenPopup("Import Credentials");
    }
  }

  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  if (ImGui::BeginPopupModal("Import Credentials", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoCollapse)) {

    if (!show_import_modal_) {
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
      return;
    }

    if (init_failed_) {
      ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                         "Error loading credentials: %s",
                         error_message_.c_str());
      ImGui::Separator();
    }

    ImGui::Text("Service account credentials are not loaded.");
    ImGui::Separator();

    if (ImGui::Button("Import Creds", ImVec2(120, 0))) {
      SDL_ShowOpenFileDialog(import_cred_callback, this, window_, json_filters,
                             1, nullptr, false);
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      show_import_modal_ = false;
    }

    ImGui::EndPopup();
    return;
  }

  if (client_) {
    ImGui::Begin(window_title);

    if (is_downloading_ || waiting_for_folder_picker_) {
      render_download_ui();
    } else {
      render_browser_ui();
    }

    ImGui::End();
  }
}

void GoogleDriveBrowser::render_download_ui() {
  ImGui::Spacing();
  ImGui::Spacing();

  if (waiting_for_folder_picker_) {
    ImGui::Text("Awaiting system folder dialog...");
    ImGui::Spacing();
    if (ImGui::Button("Cancel Selection")) {
      waiting_for_folder_picker_ = false;
    }
    return;
  }

  std::lock_guard<std::mutex> lock(ui_mutex_);

  ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s",
                     download_message_.c_str());
  ImGui::Spacing();

  char overlay[32];
  if (download_progress_ >= 1.0f) {
    snprintf(overlay, sizeof(overlay), "Complete!");
  } else if (download_progress_ > 0.0f) {
    snprintf(overlay, sizeof(overlay), "%d%%",
             static_cast<int>(download_progress_ * 100.0f));
  } else {
    snprintf(overlay, sizeof(overlay), "Starting/Unknown size...");
  }

  ImGui::ProgressBar(download_progress_, ImVec2(-1.0f, 0.0f), overlay);
  ImGui::Spacing();
  ImGui::Spacing();

  bool is_done = (download_future_.valid() &&
                  download_future_.wait_for(std::chrono::seconds(0)) ==
                      std::future_status::ready);

  if (is_done) {
    if (ImGui::Button("Return to Browser")) {
      is_downloading_ = false;
      download_message_.clear();
    }
  }
}

void GoogleDriveBrowser::render_browser_ui() {
  if (is_loading_ && fetch_future_.valid()) {
    if (fetch_future_.wait_for(std::chrono::seconds(0)) ==
        std::future_status::ready) {
      try {
        current_items_ = fetch_future_.get();
      } catch (const std::exception &e) {
        error_message_ = e.what();
        current_items_.clear();
      }
      is_loading_ = false;
    }
  }

  draw_navigation_bar();

  ImGui::Separator();
  ImGui::Spacing();

  if (!error_message_.empty()) {
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s",
                       error_message_.c_str());
  }

  if (is_loading_) {
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                       "Fetching drive contents...");
  } else if (current_items_.empty() && current_folder_id_.empty()) {
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                       "Enter a folder ID to begin browsing.");
  } else {
    draw_item_list();
  }
}

void GoogleDriveBrowser::draw_navigation_bar() {
  ImGui::BeginDisabled(history_.empty());
  if (ImGui::Button(" < ")) {
    std::string prev_id = history_.back();
    history_.pop_back();
    load_folder_async(prev_id);
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::Button(" Refresh ")) {
    load_folder_async(current_folder_id_);
  }

  ImGui::SameLine();
  ImGui::Text("  ID: ");
  ImGui::SameLine();

  ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
  bool enter_pressed = ImGui::InputText("##FolderIDInput", folder_id_input_,
                                        sizeof(folder_id_input_),
                                        ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::PopItemWidth();

  ImGui::SameLine();
  if (ImGui::Button(" Go ") || enter_pressed) {
    std::string target_id(folder_id_input_);
    if (!target_id.empty() && target_id != current_folder_id_) {
      if (!current_folder_id_.empty())
        history_.push_back(current_folder_id_);
      load_folder_async(target_id);
    } else if (target_id == current_folder_id_ && !target_id.empty()) {
      load_folder_async(current_folder_id_);
    }
  }
}

void GoogleDriveBrowser::draw_item_list() {
  static ImGuiTableFlags flags =
      ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuter |
      ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
      ImGuiTableFlags_ScrollY;

  if (ImGui::BeginTable("DriveItems", 3, flags)) {
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed,
                            180.0f);
    ImGui::TableHeadersRow();

    for (const auto &item : current_items_) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();

      bool is_folder = (item.type == FileType::Folder);
      auto mime_icon = [](const std::string &mime) -> const char * {
        if (mime == "application/vnd.google-apps.folder")
          return ICON_FA_FOLDER;
        if (mime == "image/jpeg" || mime == "image/png")
          return ICON_FA_IMAGE;
        if (mime == "text/csv")
          return ICON_FA_FILE_CSV;
        if (mime == "text/plain")
          return ICON_FA_FILE_LINES;
        return ICON_FA_FILE;
      };

      std::string display_label =
          std::string(mime_icon(item.mime_type)) + " " + item.name;

      ImGui::Selectable(display_label.c_str(), false,
                        ImGuiSelectableFlags_SpanAllColumns |
                            ImGuiSelectableFlags_AllowDoubleClick);
      if (is_folder) {
        if (ImGui::BeginPopupContextItem()) {
          if (ImGui::Selectable("Download")) {
            item_to_download_ = item;
            waiting_for_folder_picker_ = true;
            SDL_ShowOpenFolderDialog(folder_picker_callback, this, window_,
                                     nullptr, false);
          }
          ImGui::EndPopup();
        }
      }

      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) &&
          is_folder) {
        history_.push_back(current_folder_id_);
        load_folder_async(item.id);
      }

      ImGui::TableNextColumn();
      if (!is_folder)
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s",
                           format_size(item.size_bytes).c_str());

      ImGui::TableNextColumn();
      ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s",
                         item.modified_time.c_str());
    }
    ImGui::EndTable();
  }
}
