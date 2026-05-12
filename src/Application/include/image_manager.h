#pragma once
#include "include/image.h"
#include <SDL3/SDL_gpu.h>
#include <condition_variable>
#include <filesystem>
#include <queue>
#include <thread>
#include <vector>

enum class TaskType { Create, Destroy };
struct Task {
  TaskType type;
  size_t index;
  int priority;
  uint64_t generation;

  bool operator<(const Task &other) const {
    if (generation != other.generation)
      return generation < other.generation; // newer first
    return priority > other.priority;       // lower value = higher priority
  }
};

class ImageManager {
public:
  ImageManager(const std::filesystem::path image_folder)
      : image_folder_(image_folder) {
    start_thumbnail_workers(4);
  };
  ~ImageManager() {
    printf("Image Manager Destroyed\n");
    stop_thumbnail_workers();
  };

  Image *load_image(int index);
  Image *load_next();
  Image *load_previous();

  void load_folder(SDL_GPUDevice *device);
  void cleanup_stale_images();

  void start_thumbnail_workers(size_t num_threads);
  void load_thumbnail_range(int start, int end);
  void schedule_thumbnail_cleanup(int start, int end);

  std::filesystem::path folder() { return image_folder_; };
  std::filesystem::path current_image_path() {
    return image_order[index].filename;
  };
  std::filesystem::path image_path_from_index(int i) {
    return image_order[i].filename;
  };

  int size = 0;
  int index = 0;

  int last_thumbnail_loaded = 0;
  std::vector<Image> image_order;
  Image *current_image = nullptr;

private:
  std::filesystem::path image_folder_;
  std::vector<Image *> stale_images;

  // THUMBNAIL SHENANIGANS
  std::atomic<uint64_t> current_generation{0};
  void stop_thumbnail_workers();

  std::priority_queue<Task> task_queue;
  std::vector<std::thread> thumbnail_threads;
  std::mutex thumbnail_mutex;
  std::condition_variable thumbnail_cv;
  std::atomic<bool> stop_flag{false};
};
