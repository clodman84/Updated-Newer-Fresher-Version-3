#ifndef IMAGE_H
#define IMAGE_H

#define _CRT_SECURE_NO_WARNINGS

#include "imgui.h"
#include "sqlite3.h"
#include <SDL3/SDL.h>
#include <string>
#include <vector>

#define MAX(A, B) (((A) >= (B)) ? (A) : (B))

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

void prepare_database();

class Database {
public:
  Database();
  ~Database();
  void read_csv(const std::string &filename);
  void show_loaded_csv(); // call this after reading a csv to verify the state
  void store_loaded_csv();
  void insert_data();

private:
  std::string db_filename = "./Data/database.db";
  std::vector<std::vector<std::string>> loaded;
  sqlite3 *db;
};

#endif // !IMAGE_H
