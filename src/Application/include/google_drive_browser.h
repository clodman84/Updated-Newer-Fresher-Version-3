#pragma once

#include "google_drive.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <filesystem>
#include <future>
#include <mutex>
#include <string>
#include <vector>

class GoogleDriveBrowser {
public:
  explicit GoogleDriveBrowser(SDL_Window *window,
                              const std::filesystem::path &credentials_path);

  void render_window(const char *window_title);

  void start_download(const std::string &dest_folder);
  void cancel_download_state();
  DriveItem item_to_download_;

private:
  SDL_Window *window_;
  DriveClient client_;

  std::string current_folder_id_;
  std::vector<DriveItem> current_items_;
  std::vector<std::string> history_;
  char folder_id_input_[256] = "";

  bool is_loading_ = false;
  std::string error_message_;
  std::future<std::vector<DriveItem>> fetch_future_;

  std::future<void> download_future_;
  std::atomic<bool> is_downloading_{false};
  std::atomic<bool> waiting_for_folder_picker_{false};
  std::atomic<float> download_progress_{0.0f};
  std::mutex ui_mutex_;
  std::string download_message_;

  void load_folder_async(const std::string &folder_id);

  void render_browser_ui();
  void render_download_ui();
  void draw_navigation_bar();
  void draw_item_list();
  static std::string format_size(long long bytes);
};
