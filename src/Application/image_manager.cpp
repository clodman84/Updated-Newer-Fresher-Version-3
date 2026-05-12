#include "include/image_manager.h"

#include <algorithm>
#include <cstdint>
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

Image *ImageManager::load_image(int idx) {
#ifdef TRACY_ENABLE
  ZoneScopedN("ImageManager::load_image");
#endif
  if (image_order.empty() || idx < 0 ||
      idx >= static_cast<int>(image_order.size())) {
    return nullptr;
  }
  index = idx;

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
  return load_image(index);
}

Image *ImageManager::load_previous() {
  if (image_order.empty()) {
    return nullptr;
  }
  index = index <= 0 ? static_cast<int>(image_order.size()) - 1 : index - 1;
  return load_image(index);
}

void ImageManager::start_thumbnail_workers(size_t num_threads) {
  for (size_t i = 0; i < num_threads; ++i) {
    thumbnail_threads.emplace_back([this]() {
      while (true) {
        Task task;

        {
          std::unique_lock<std::mutex> lock(thumbnail_mutex);
          thumbnail_cv.wait(
              lock, [this]() { return !task_queue.empty() || stop_flag; });

          if (stop_flag && task_queue.empty())
            return;

          task = task_queue.top();
          task_queue.pop();
        }

        if (task.generation != current_generation.load())
          continue;

        if (task.type == TaskType::Create) {
          if (task.generation != current_generation.load())
            continue;
          image_order[task.index].load_thumbnail();
        } else {
          if (task.generation != current_generation.load())
            continue;
          image_order[task.index].destroy_thumbnail();
        }
      }
    });
  }
}

void ImageManager::stop_thumbnail_workers() {
  stop_flag = true;
  thumbnail_cv.notify_all();
  for (auto &t : thumbnail_threads)
    if (t.joinable())
      t.join();
}

void ImageManager::load_thumbnail_range(int start, int end) {
  uint64_t gen = ++current_generation;
  std::lock_guard<std::mutex> lock(thumbnail_mutex);

  for (int i = start; i <= end; ++i) {
    task_queue.push(Task{TaskType::Create, (size_t)i, 0, gen});
  }

  thumbnail_cv.notify_all();
}

void ImageManager::schedule_thumbnail_cleanup(int start, int end) {
  std::lock_guard<std::mutex> lock(thumbnail_mutex);
  for (size_t i = 0; i < image_order.size(); ++i) {
    if (i < start || i > end) {
      task_queue.push(Task{TaskType::Destroy, i,
                           10000, // very low priority
                           current_generation});
    }
  }
  thumbnail_cv.notify_all();
}
