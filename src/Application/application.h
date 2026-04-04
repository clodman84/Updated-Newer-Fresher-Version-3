#ifndef IMAGE_H
#define IMAGE_H

#include <cstddef>
#define _CRT_SECURE_NO_WARNINGS

#include "imgui.h"
#include "json.hpp"
#include "sqlite3.h"
#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

inline std::string natural_time(double seconds) {
  struct Unit {
    const char *label;
    double size;
  };
  constexpr Unit units[] = {
      {"mi", 60.0},
      {" s", 1.0},
      {"ms", 1e-3},
      {"us", 1e-6},
  };
  const double absolute = std::abs(seconds);
  for (const auto &unit : units) {
    if (absolute > unit.size) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%6.2f %s", seconds / unit.size, unit.label);
      return buf;
    }
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%6.2f ns", seconds / 1e-9);
  return buf;
}

class ScopedTimer {
public:
  using Clock = std::chrono::high_resolution_clock;

  explicit ScopedTimer(std::string &output)
      : output_(output), start_(Clock::now()) {}

  ~ScopedTimer() {
    const auto end = Clock::now();
    const double seconds = std::chrono::duration<double>(end - start_).count();
    output_ = natural_time(seconds);
  }

private:
  std::string &output_;
  std::chrono::time_point<Clock> start_;
};

class Image {
public:
  Image(SDL_GPUDevice *device, const std::filesystem::path &filename);
  ~Image();

  Image(Image &&other) noexcept;
  Image &operator=(Image &&other) noexcept = delete;
  Image(const Image &) = delete;
  Image &operator=(const Image &) = delete;

  bool is_valid() const;

  SDL_GPUTexture *texture = nullptr;
  int width = 0;
  int height = 0;
  std::filesystem::path filename;

private:
  SDL_GPUDevice *device = nullptr;
};

struct Thumbnail {
  SDL_GPUTexture *texture = nullptr;
  int width = 0;
  int height = 0;
};

struct BillEntry {
  std::string name;
  int count = 0;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE(BillEntry, name, count);
};

struct FaceRect {
  ImVec2 bounds_min;
  ImVec2 bounds_max;
  int count;
};

std::vector<FaceRect> scan_faces(std::filesystem::path);

class ImageEditor {
public:
  ImageEditor(SDL_GPUDevice *device) : device(device) {};
  ~ImageEditor();
  SDL_GPUTexture *preview_texture = nullptr;
  std::filesystem::path image_path;
  int width = 0;
  int height = 0;
  void load_path(std::filesystem::path);
  void render_preview();
  float get_zoom() const { return zoom; }
  ImVec2 get_pan() const { return pan; }
  void set_view(float next_zoom, ImVec2 next_pan) {
    zoom = next_zoom;
    pan = next_pan;
  }

private:
  SDL_GPUDevice *device = nullptr;
  float zoom = 0.0f;
  ImVec2 canvas_size = ImVec2(0.0f, 0.0f);
  ImVec2 pan = ImVec2(0.0f, 0.0f);
  void reset_view_to_image();
};

class ImageManager {
public:
  ImageManager(SDL_GPUDevice *device,
               const std::filesystem::path &image_folder);
  ~ImageManager();

  ImageManager(ImageManager &&other) noexcept;
  ImageManager &operator=(ImageManager &&other) noexcept;
  ImageManager(const ImageManager &) = delete;
  ImageManager &operator=(const ImageManager &) = delete;

  ImageEditor editor;

  Image *load_image();
  Image *load_next();
  Image *load_previous();
  bool select_image_by_name(const std::string &name);
  const std::vector<std::string> &get_thumbnail_order() const;
  const Thumbnail *get_thumbnail(const std::string &name) const;
  int get_image_index(const std::string &name) const;
  const Image *get_current_image() const;
  const std::filesystem::path *current_image_path() const;
  bool has_images() const;
  const std::filesystem::path &folder() const;

  void render_manager();
  void load_thumbnails();

  int index = 0;
  int size = 0;
  bool with_detection = false;

private:
  void load_folder(const std::filesystem::path &folder);
  void clear_current_image();
  void clear_thumbnails();
  void reset_view_to_image();
  void queue_image_by_index(int next_index);
  void apply_pending_selection();
  void render_viewer();
  void render_editor();
  void render_carousel(float carousel_height);

  std::filesystem::path image_folder_;
  std::unique_ptr<Image> current_image_;
  std::vector<std::string> image_names;
  std::vector<std::string> thumbnail_order;
  std::map<std::string, Thumbnail> thumbnails;
  SDL_GPUDevice *device = nullptr;
  float zoom = 0.0f;
  ImVec2 canvas_size = ImVec2(0.0f, 0.0f);
  ImVec2 pan = ImVec2(0.0f, 0.0f);
  int pending_index = -1;
  int last_drawn_index = -1;
  bool with_preview = false;
  bool link_preview_viewer = false;
};

void prepare_database();

enum TokenType { FTS_SEARCH, BHAWAN_SEARCH, ID_SEARCH, OR, AND, LPAR, RPAR };

struct ExportInfo {
  std::string bhawan;
  std::string roomno;
};

class Database {
public:
  Database();
  ~Database();

  void read_csv(const std::string &filename);
  void render_loaded_csv();
  void insert_data();
  void search(TokenType search_type, std::string search_query,
              std::vector<std::array<std::string, 4>> &search_results);
  ExportInfo get_export_information_from_id(std::string id);

  bool show_loaded_csv = false;

private:
  using CsvRow = std::array<std::string, 5>;

  std::string modify_query_for_id(std::string query);
  bool parse_csv_row(const std::string &line, CsvRow &row) const;
  bool begin_transaction() const;
  bool commit_transaction() const;
  bool rollback_transaction() const;
  bool execute_sql(const char *sql) const;
  void clear_loaded_csv();

  std::string db_filename = "./Data/database.db";
  std::vector<CsvRow> loaded;
  sqlite3 *db = nullptr;
  sqlite3_stmt *fts_search = nullptr;
  sqlite3_stmt *bhawan_search = nullptr;
  sqlite3_stmt *id_search = nullptr;
  sqlite3_stmt *get_export_info_stmt = nullptr;
};

struct PendingImage {
  std::filesystem::path source;
  std::filesystem::path destination;
  std::string watermark;
  std::string label;
};

class Session {
public:
  Session(Database *database, std::filesystem::path path,
          SDL_GPUDevice *device);
  ~Session();

  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

  void render_searcher();
  void render_billed();
  void handle_keyboard_nav();
  void open_export_modal();
  void render_export_modal();
  const std::filesystem::path &session_path() const;
  const std::filesystem::path &image_folder() const;

  ImageManager manager;
  std::filesystem::path path;
  bool draw_exporting = false;

private:
  using BillMap = std::unordered_map<std::filesystem::path,
                                     std::map<std::string, BillEntry>>;

  enum class KeyboardNavMode { Search, Billed };

  const std::filesystem::path *current_image_path() const;
  std::map<std::string, BillEntry> *current_bill_entries();
  const std::map<std::string, BillEntry> *current_bill_entries() const;
  int visible_billed_entry_count() const;
  void sync_search_selection_bounds();
  void sync_billed_selection_bounds();
  void handle_search_keyboard_nav();
  void handle_billed_keyboard_nav();
  void render_same_as_popup();
  void render_search_results_table();
  void render_billed_table(std::map<std::string, BillEntry> &entries);
  void load_existing_bill();
  void increment_for_id(const std::string &id, const std::string &name);
  void autosave();
  void evaluate();
  void prepare_export_queue();
  void start_export();
  void finish_export_if_ready();
  void export_images();
  void process_pending_image(const PendingImage &image, size_t worker_index);
  void append_bill_from_image(const std::filesystem::path &source_image);

  Database *database = nullptr;
  std::string search_query;
  std::vector<std::array<std::string, 4>> search_results;
  BillMap bill;
  std::vector<PendingImage> pending;
  std::thread export_worker;
  std::mutex export_status_mutex;
  std::vector<std::string> export_active_items;
  std::vector<unsigned char> export_font_data;
  std::string export_status_message = "Ready to export";
  std::string export_output_directory;
  std::string quote;
  bool export_apply_watermark = true;
  KeyboardNavMode keyboard_nav_mode = KeyboardNavMode::Search;
  int selected_search_index = 0;
  int selected_billed_index = 0;
  bool focus_search_on_next_frame = false;
  bool focus_billed_on_next_frame = false;
  std::atomic<int> export_progress{0};
  int export_total = 0;
  bool exporting = false;
  bool export_completed = false;
  bool draw_same_as_popup = false;
};

#endif // !IMAGE_H
