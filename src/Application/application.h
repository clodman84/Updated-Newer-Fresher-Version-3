#ifndef IMAGE_H
#define IMAGE_H
#include <map>
#include <unordered_map>
#define _CRT_SECURE_NO_WARNINGS
#include "imgui.h"
#include "sqlite3.h"
#include <SDL3/SDL.h>
#include <array>
#include <chrono>
#include <string>
#include <vector>
#define MAX(A, B) (((A) >= (B)) ? (A) : (B))

// ─────────────────────────────────────────────────────────────────────────────
// Timer Utilities
// ─────────────────────────────────────────────────────────────────────────────

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
  double absolute = std::abs(seconds);
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
  ScopedTimer(std::string &output) : output_(output), start_(Clock::now()) {}
  ~ScopedTimer() {
    auto end = Clock::now();
    double seconds = std::chrono::duration<double>(end - start_).count();
    output_ = natural_time(seconds);
  }

private:
  std::string &output_;
  std::chrono::time_point<Clock> start_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Image
// Owns a single SDL_GPUTexture. Move-only — copying would double-release the
// GPU handle.
// ─────────────────────────────────────────────────────────────────────────────

class Image {
public:
  Image(SDL_GPUDevice *device, const char *filename);
  ~Image();

  // Move-only
  Image(Image &&other) noexcept;
  Image &operator=(Image &&other) noexcept = delete; // add if needed
  Image(const Image &) = delete;
  Image &operator=(const Image &) = delete;

  SDL_GPUTexture *texture;
  int width;
  int height;
  const char *filename;

private:
  SDL_GPUDevice *device;
};

typedef struct {
  SDL_GPUTexture *texture;
  int width;
  int height;
} Thumbnail_T;

// ─────────────────────────────────────────────────────────────────────────────
// ImageManager
// Owns a collection of thumbnail textures and the currently-displayed Image.
// Move-only — GPU texture handles must not be duplicated.
//
// WHY THIS MATTERS: main.cpp stores Sessions (which contain an ImageManager)
// in a std::deque<Session>. emplace_back on a deque can trigger reallocation
// which calls the copy or move constructor. If copy is used, two objects own
// the same SDL_GPUTexture* pointers and both will call SDL_ReleaseGPUTexture
// on destruction → double-free, VRAM leak, and Vulkan descriptor crash.
// Deleting copy and providing move prevents this entirely.
// ─────────────────────────────────────────────────────────────────────────────

class ImageManager {
public:
  ImageManager(SDL_GPUDevice *device, const char *image_folder);
  ~ImageManager();

  // Move-only
  ImageManager(ImageManager &&other) noexcept;
  ImageManager &operator=(ImageManager &&other) noexcept;
  ImageManager(const ImageManager &) = delete;
  ImageManager &operator=(const ImageManager &) = delete;

  Image *load_image();
  Image *load_next();
  Image *load_previous();

  int index;
  int size;
  void draw_manager(ImGuiIO *io);
  void load_thumbnails();
  std::string imageFolder;
  Image *current_image;

private:
  void load_folder(const char *);
  std::vector<std::string> image_names;
  std::vector<std::string> thumbnail_order;
  std::map<std::string, Thumbnail_T> thumbnails;
  SDL_GPUDevice *device;
  float zoom = 1.0f;
  ImVec2 pan = ImVec2(0, 0);
  int pending_index = -1; // carousel click deferred one frame to avoid
                          // destroying a texture still referenced in draw list
  int last_drawn_index = -1;
};

// ─────────────────────────────────────────────────────────────────────────────
// Database
// ─────────────────────────────────────────────────────────────────────────────

void prepare_database();

enum SearchType { FTS_SEARCH, BHAWAN_SEARCH, ID_SEARCH };

class Database {
public:
  Database();
  ~Database();
  void read_csv(const std::string &filename);
  void render_loaded_csv();
  void insert_data();
  void search(SearchType search_type, std::string &search_query,
              std::vector<std::array<std::string, 4>> &search_results);
  bool show_loaded_csv = false;

private:
  std::string db_filename = "./Data/database.db";
  std::vector<std::vector<std::string>> loaded;
  sqlite3 *db = nullptr;
  sqlite3_stmt *fts_search = nullptr;
  sqlite3_stmt *bhawan_search = nullptr;
  sqlite3_stmt *id_search = nullptr;
  std::string modify_query_for_id(std::string query);
  void store_loaded_csv();
};

typedef struct {
  std::string name;
  int count;
} BillEntry;

// ─────────────────────────────────────────────────────────────────────────────
// Session
// Contains an ImageManager by value, so it must also be move-only.
// In main.cpp, store as std::deque<std::unique_ptr<Session>> or ensure
// Session is only ever moved (never copied) when inserted into the deque.
// ─────────────────────────────────────────────────────────────────────────────

class Session {
public:
  Session(Database *database, std::string path, SDL_GPUDevice *device)
      : database(database), path(path), manager(device, path.c_str()) {}
  ~Session() {}

  // Move-only (ImageManager inside is move-only)
  Session(Session &&) = default;
  Session &operator=(Session &&) = default;
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

  void render_searcher();
  void render_billed();
  ImageManager manager;

private:
  Database *database;
  std::string path;
  std::string search_query = "";
  std::vector<std::array<std::string, 4>> search_results;
  std::unordered_map<std::string, std::map<std::string, BillEntry>> bill;
  void increment_for_id(std::string, std::string);
  void autosave();
};

#endif // !IMAGE_H
