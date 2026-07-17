#pragma once

#include "google_drive.h"
#include "include/dino_jumpy.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class GoogleDriveBrowser {
public:
  GoogleDriveBrowser(std::shared_ptr<DriveClient> client, SDL_Window *window);
  void render();
  bool is_busy() const;

private:
  std::shared_ptr<DriveClient> client_;
  SDL_Window *window_;

  std::string current_folder_id_;
  std::vector<DriveItem> current_items_;
  std::vector<std::string> nav_history_; // back-stack of folder IDs
  char id_buf_[256];
  std::string error_;

  bool fetching_ = false;
  std::future<std::vector<DriveItem>> fetch_future_;

  enum class DownloadState { Idle, WaitingForPath, Active, Done };
  DownloadState dl_state_ = DownloadState::Idle;
  DriveItem dl_item_;
  std::future<void> dl_future_;
  std::atomic<float> dl_progress_{0.f};
  std::mutex dl_msg_mutex_;
  std::string dl_message_;
  Game dino_jumpy;

  void draw_toolbar();
  void draw_file_table();
  void draw_download_overlay();
  void draw_error_bar();

  void navigate_to(const std::string &folder_id, bool push_history = true);
  void poll_fetch();
  void begin_download(const DriveItem &item, const std::string &dest_path);
  void set_message(const std::string &msg);

  static const char *icon_for(const DriveItem &item);
  static std::string friendly_size(long long bytes);
  static std::string friendly_time(const std::string &rfc3339);

  static void SDLCALL on_folder_picked(void *userdata,
                                       const char *const *filelist, int filter);
};
