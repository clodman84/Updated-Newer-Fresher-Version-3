#include "include/image_manager.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <vector>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

bool is_image_file(const std::filesystem::path path) {
  if (!path.has_extension())
    return false;

  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  static const std::vector<std::string> valid_exts = {".jpg", ".jpeg"};

  return std::find(valid_exts.begin(), valid_exts.end(), ext) !=
         valid_exts.end();
}

void ImageManager::load_folder(SDL_GPUDevice *device) {
  image_order.clear();

  if (!std::filesystem::exists(image_folder_) ||
      !std::filesystem::is_directory(image_folder_)) {
    std::cerr << "Image folder is missing or invalid: "
              << image_folder_.string() << std::endl;
    size = 0;
    return;
  }

  for (const auto &entry : std::filesystem::directory_iterator(image_folder_)) {
    if (!entry.is_regular_file())
      continue;

    const auto &path = entry.path();
    if (is_image_file(path)) {
      image_order.emplace_back(path, device);
    }
  }

  std::sort(
      image_order.begin(), image_order.end(),
      [](const Image &a, const Image &b) { return a.filename < b.filename; });

  size = image_order.size();
}

void ImageManager::cleanup_stale_images() {
  for (auto image : stale_images) {
    image->destroy_texture();
  }
  stale_images.clear();
}

Image *ImageManager::load_image() {
#ifdef TRACY_ENABLE
  ZoneScopedN("ImageManager::load_image");
#endif
  if (image_order.empty() || index < 0 ||
      index >= static_cast<int>(image_order.size())) {
    return nullptr;
  }

  if (current_image != nullptr &&
      current_image->filename == image_order[index].filename)
    return current_image;
  if (current_image != nullptr)
    stale_images.push_back(current_image);

  image_order[index].load_halfres();
  current_image = &image_order[index];
  return current_image;
}

Image *ImageManager::load_next() {
  if (image_order.empty()) {
    return nullptr;
  }
  index = index >= static_cast<int>(image_order.size()) - 1 ? 0 : index + 1;
  return load_image();
}

Image *ImageManager::load_previous() {
  if (image_order.empty()) {
    return nullptr;
  }
  index = index <= 0 ? static_cast<int>(image_order.size()) - 1 : index - 1;
  return load_image();
}

void ImageManager::load_image_thumbnails(int start_idx, int end_idx) {
#ifdef TRACY_ENABLE
  ZoneScopedN("ImageManager::load_image_thumbnails");
#endif
  std::vector<std::thread> workers;
  workers.reserve(end_idx - start_idx);

  for (size_t index = start_idx; index < end_idx - start_idx; ++index) {
    workers.emplace_back(
        [this, index]() { image_order[index].load_thumbnail(); });
  }

  for (auto &worker : workers) {
    worker.join();
  }
}
