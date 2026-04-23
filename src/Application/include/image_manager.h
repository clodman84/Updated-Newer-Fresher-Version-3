#pragma once
#include "include/image.h"
#include <SDL3/SDL_gpu.h>
#include <filesystem>
#include <vector>

class ImageManager {
public:
  ImageManager(const std::filesystem::path image_folder)
      : image_folder_(image_folder) {};
  ~ImageManager() {
    for (auto image : image_order) {
      image.destroy_thumbnail();
    }
  };

  Image *load_image();
  Image *load_next();
  Image *load_previous();

  void load_folder(SDL_GPUDevice *device);
  void load_image_thumbnails(int start_idx, int end_idx);
  void cleanup_stale_images();

  std::filesystem::path folder() { return image_folder_; };
  std::filesystem::path current_image_path() {
    return image_order[index].filename;
  };
  std::filesystem::path image_path_from_index(int i) {
    return image_order[i].filename;
  };

  int index = 0;
  int size = 0;
  std::vector<Image> image_order;
  Image *current_image = nullptr;

private:
  std::filesystem::path image_folder_;
  std::vector<Image *> stale_images;
};
