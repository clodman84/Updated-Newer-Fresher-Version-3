#pragma once

#include "include/database.h"
#include "include/detection.h"
#include "include/export_manager.h"
#include "include/image.h"
#include "include/image_editor.h"
#include "include/image_manager.h"

#include <SDL3/SDL_gpu.h>
#include <filesystem>
#include <imgui.h>
#include <set>
#include <string>
#include <vector>

class Session {
public:
  Session(std::filesystem::path folder_path, SDL_GPUDevice *device)
      : folder_path(folder_path), export_manager(folder_path),
        image_manager(folder_path), editor(device) {};
  ~Session() = default;

  void handle_keyboard_nav();

  void render_search_results_table();
  void render_searcher();

  void render_billed_table();
  void render_billed();

  void render_image_panel();

  void render_carousel(float carousel_height);
  void render_control_panel();

  std::filesystem::path folder_path;
  ExportManager export_manager;
  ImageManager image_manager;

private:
  enum class KeyboardNavMode { Search, Billed };
  std::vector<Image> marked_for_destruction;

  void sync_search_selection_bounds();
  void sync_billed_selection_bounds();
  void handle_search_keyboard_nav();
  void handle_billed_keyboard_nav();
  void evaluate();

  void render_main_image();
  void reset_view_to_image();
  ImVec2 canvas_size;
  ImVec2 pan;
  float zoom;
  bool with_detection = false;

  bool with_preview = false;
  bool link_preview_viewer = false;

  bool draw_with_preview = false;

  Database database;
  ImageEditor editor;
  FaceDetector detector;

  std::set<std::filesystem::path> selection_storage;

  std::string search_query;
  std::vector<std::array<std::string, 4>> search_results;

  KeyboardNavMode keyboard_nav_mode = KeyboardNavMode::Search;
  int selected_search_index = 0;
  int selected_billed_index = 0;
  bool focus_search_on_next_frame = false;
  bool focus_billed_on_next_frame = false;
  int last_drawn_index = -1;
  int last_clicked_index = -1;

  int visible_start = -1;
  int visible_end = -1;
};
