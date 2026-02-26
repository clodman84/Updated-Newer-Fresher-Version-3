#ifndef IMAGE_H
#define IMAGE_H

#define _CRT_SECURE_NO_WARNINGS

#include "imgui.h"
#include "sqlite3.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <string>
#include <vector>
#define MAX(A, B) (((A) >= (B)) ? (A) : (B))

// Timer Utilities
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

// Imejes (bogos binted)
bool LoadTextureFromMemory(const void *data, size_t data_size,
                           SDL_GPUDevice *device, SDL_GPUTexture **out_texture,
                           int *out_width, int *out_height);
bool LoadTextureFromFile(const char *file_name, SDL_GPUDevice *device,
                         SDL_GPUTexture **out_texture, int *out_width,
                         int *out_height);
void DestroyTexture(SDL_GPUDevice *device, SDL_GPUTexture *texture);

class Image {
public:
  Image(SDL_GPUDevice *device, const char *filename);
  ~Image();
  SDL_GPUTexture *texture;
  int width;
  int height;

private:
  const char *filename;
  SDL_GPUDevice *device;
};

class ImageManager {
public:
  ImageManager(SDL_GPUDevice *device, const char *imageFolder);
  ~ImageManager();
  Image *loadImage();
  Image *loadNext();
  Image *loadPrevious();
  int index;
  int size;
  void drawManager(ImGuiIO *io);
  const char *imageFolder;

private:
  Image *current_image;
  Image *previous_image;
  Image *next_image;
  void loadFolder(const char *);
  std::vector<std::string> images;
  SDL_GPUDevice *device;
};

// the overflowing laundro bag
void prepare_database();

class Database {
public:
  Database();
  ~Database();
  void read_csv(const std::string &filename);
  void render_loaded_csv(); // call this after reading a csv to verify the state
  void store_loaded_csv();
  void insert_data();
  void render_searcher();
  void search();

private:
  std::string db_filename = "./Data/database.db";
  std::vector<std::vector<std::string>> loaded;
  sqlite3 *db = nullptr;
  std::string processing_time;
  std::string search_query;
  sqlite3_stmt *fts_search = nullptr;
  std::vector<std::vector<std::string>> fts_results;
  bool show_loaded_csv = true;
};

#endif // !IMAGE_H
