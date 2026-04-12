#include "application.h"

#include "SDL3/SDL_gpu.h"
#include "gpu_utils.h"
#include "imgui.h"
#include <memory>

#define _CRT_SECURE_NO_WARNINGS
#define STB_TRUETYPE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"
#include "stb_truetype.h"

#include <SDL3/SDL.h>
#include <gegl.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

namespace {

bool is_image_file(const char *name) {
  const char *ext = SDL_strrchr(name, '.');
  if (ext == nullptr) {
    return false;
  }
  return SDL_strcasecmp(ext, ".png") == 0 || SDL_strcasecmp(ext, ".jpg") == 0 ||
         SDL_strcasecmp(ext, ".jpeg") == 0 ||
         SDL_strcasecmp(ext, ".bmp") == 0 || SDL_strcasecmp(ext, ".tga") == 0;
}

static SDL_EnumerationResult enumerate_cb(void *userdata, const char *dirname,
                                          const char *fname) {
  if (fname[0] == '.' || !is_image_file(fname)) {
    return SDL_ENUM_CONTINUE;
  }

  auto *image_names = static_cast<std::vector<std::string> *>(userdata);
  image_names->push_back(std::string(dirname) + fname);
  return SDL_ENUM_CONTINUE;
}

struct PendingThumbnail {
  std::string file_name;
  unsigned char *pixel_data = nullptr;
  int width = 0;
  int height = 0;
};

} // namespace

Image::Image(SDL_GPUDevice *device, const std::filesystem::path &filename)
    : filename(filename), device(device) {
#ifdef TRACY_ENABLE
  ZoneScopedN("Image::Image");
#endif
  unsigned char *image_data =
      load_texture_data_from_file(filename, &width, &height);
  if (image_data == nullptr) {
    return;
  }

  if (!upload_texture_data_to_gpu(image_data, width, height, device,
                                  &texture)) {
    texture = nullptr;
    width = 0;
    height = 0;
  }
  stbi_image_free(image_data);
}

Image::~Image() {
  if (texture != nullptr && device != nullptr) {
    SDL_ReleaseGPUTexture(device, texture);
  }
}

Image::Image(Image &&other) noexcept
    : texture(other.texture), width(other.width), height(other.height),
      filename(std::move(other.filename)), device(other.device) {
  other.texture = nullptr;
  other.width = 0;
  other.height = 0;
  other.device = nullptr;
}

bool Image::is_valid() const {
  return texture != nullptr && width > 0 && height > 0;
}

void Session::process_pending_image(const PendingImage &image,
                                    size_t worker_index) {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::process_pending_image");
#endif
  {
    std::lock_guard<std::mutex> lock(export_status_mutex);
    if (worker_index < export_active_items.size()) {
      export_active_items[worker_index] = image.label;
    }
  }

  int width = 0;
  int height = 0;
  int channels = 0;
  unsigned char *pixels =
      stbi_load(image.source.string().c_str(), &width, &height, &channels, 3);
  if (pixels == nullptr) {
    export_progress.fetch_add(1);
    std::lock_guard<std::mutex> lock(export_status_mutex);
    if (worker_index < export_active_items.size()) {
      export_active_items[worker_index] = "Skipped unreadable image";
    }
    std::cerr << "Failed to read export source image: " << image.source.string()
              << std::endl;
    return;
  }

  if (export_apply_watermark && !export_font_data.empty()) {
    stbtt_fontinfo font;
    stbtt_InitFont(&font, export_font_data.data(), 0);

    const float scale = stbtt_ScaleForPixelHeight(&font, height * 0.04f);
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);

    int text_width = 0;
    for (const char *c = image.watermark.c_str(); *c != '\0'; ++c) {
      int advance = 0;
      int lsb = 0;
      stbtt_GetCodepointHMetrics(&font, *c, &advance, &lsb);
      text_width += static_cast<int>(advance * scale);
    }

    const int text_height = static_cast<int>((ascent - descent) * scale);
    const int origin_x = width - text_width - 12;
    const int origin_y = height - text_height - 12;
    const int baseline = static_cast<int>(ascent * scale);

    struct Pass {
      int ox;
      int oy;
      unsigned char r;
      unsigned char g;
      unsigned char b;
    };

    for (const Pass pass : {Pass{2, 2, 0, 0, 0}, Pass{0, 0, 255, 255, 0}}) {
      int cursor_x = origin_x + pass.ox;
      const int cursor_y = origin_y + pass.oy;
      for (const char *c = image.watermark.c_str(); *c != '\0'; ++c) {
        int advance = 0;
        int lsb = 0;
        stbtt_GetCodepointHMetrics(&font, *c, &advance, &lsb);

        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBox(&font, *c, scale, scale, &x0, &y0, &x1,
                                    &y1);
        int glyph_w = x1 - x0;
        int glyph_h = y1 - y0;
        if (glyph_w > 0 && glyph_h > 0) {
          int gx0 = 0;
          int gy0 = 0;
          unsigned char *bitmap = stbtt_GetCodepointBitmap(
              &font, scale, scale, *c, &glyph_w, &glyph_h, &gx0, &gy0);
          for (int gy = 0; gy < glyph_h; ++gy) {
            for (int gx = 0; gx < glyph_w; ++gx) {
              const int px = cursor_x + gx0 + gx;
              const int py = cursor_y + baseline + gy0 + gy;
              if (px < 0 || px >= width || py < 0 || py >= height) {
                continue;
              }
              if (bitmap[gy * glyph_w + gx] > 128) {
                unsigned char *dst = pixels + (py * width + px) * 3;
                dst[0] = pass.r;
                dst[1] = pass.g;
                dst[2] = pass.b;
              }
            }
          }
          stbtt_FreeBitmap(bitmap, nullptr);
        }
        cursor_x += static_cast<int>(advance * scale);
      }
    }
  }

  stbi_write_jpg(image.destination.string().c_str(), width, height, 3, pixels,
                 95);
  stbi_image_free(pixels);
  export_progress.fetch_add(1);

  std::lock_guard<std::mutex> lock(export_status_mutex);
  if (worker_index < export_active_items.size()) {
    export_active_items[worker_index] = "Idle";
  }
}

void Session::export_images() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::export_images");
#endif
  if (pending.empty()) {
    return;
  }

  const unsigned int hardware_threads = std::thread::hardware_concurrency();
  const size_t worker_count = std::max<size_t>(
      1, std::min(pending.size(), static_cast<size_t>(hardware_threads == 0
                                                          ? 4
                                                          : hardware_threads)));

  {
    std::lock_guard<std::mutex> lock(export_status_mutex);
    export_active_items.assign(worker_count, "Waiting to start");
  }

  std::atomic<size_t> next_index{0};
  std::vector<std::thread> workers;
  workers.reserve(worker_count);

  for (size_t worker_index = 0; worker_index < worker_count; ++worker_index) {
    workers.emplace_back([this, worker_index, &next_index]() {
      while (true) {
        const size_t item_index = next_index.fetch_add(1);
        if (item_index >= pending.size()) {
          std::lock_guard<std::mutex> lock(export_status_mutex);
          if (worker_index < export_active_items.size()) {
            export_active_items[worker_index] = "Idle";
          }
          break;
        }
        process_pending_image(pending[item_index], worker_index);
      }
    });
  }

  for (auto &worker : workers) {
    worker.join();
  }
}

void ImageManager::load_thumbnails() {
#ifdef TRACY_ENABLE
  ZoneScopedN("ImageManager::load_thumbnails");
#endif
  std::vector<PendingThumbnail> pending_thumbnails(image_names.size());
  std::vector<std::thread> workers;
  workers.reserve(image_names.size());

  for (size_t index = 0; index < image_names.size(); ++index) {
    pending_thumbnails[index].file_name = image_names[index];
    workers.emplace_back([index, &pending_thumbnails]() {
#ifdef TRACY_ENABLE
      ZoneScopedN("ThumbnailWorker");
#endif
      int src_w = 0;
      int src_h = 0;
      unsigned char *src = load_texture_data_from_file(
          pending_thumbnails[index].file_name, &src_w, &src_h);
      if (src == nullptr || src_h <= 0) {
        return;
      }

      constexpr int dst_h = 200;
      const float factor = static_cast<float>(dst_h) / src_h;
      const int dst_w = std::max(1, static_cast<int>(src_w * factor));

      unsigned char *dst = resize_image_rgba8(src, src_w, src_h, dst_w, dst_h);
      stbi_image_free(src);

      pending_thumbnails[index].pixel_data = dst;
      pending_thumbnails[index].width = dst_w;
      pending_thumbnails[index].height = dst_h;
    });
  }

  for (auto &worker : workers) {
    worker.join();
  }

  thumbnail_order.clear();
  for (auto &thumbnail : pending_thumbnails) {
    if (thumbnail.pixel_data == nullptr) {
      continue;
    }

    SDL_GPUTexture *texture = nullptr;
    if (upload_texture_data_to_gpu(thumbnail.pixel_data, thumbnail.width,
                                   thumbnail.height, device, &texture) &&
        texture != nullptr) {
      thumbnails[thumbnail.file_name] =
          Thumbnail{texture, thumbnail.width, thumbnail.height};
      thumbnail_order.push_back(thumbnail.file_name);
    }
    stbi_image_free(thumbnail.pixel_data);
  }
}

ImageManager::ImageManager(SDL_GPUDevice *device,
                           const std::filesystem::path &image_folder,
                           std::shared_ptr<ImageEditor> editor)
    : image_folder_(image_folder), device(device), editor(editor) {
#ifdef TRACY_ENABLE
  ZoneScopedN("ImageManager::ImageManager");
#endif
  load_folder(image_folder);
  load_thumbnails();
  load_image();
}

ImageManager::~ImageManager() {
  clear_current_image();
  clear_thumbnails();
}

ImageManager::ImageManager(ImageManager &&other) noexcept
    : index(other.index), size(other.size),
      image_folder_(std::move(other.image_folder_)),
      current_image_(std::move(other.current_image_)),
      image_names(std::move(other.image_names)),
      thumbnail_order(std::move(other.thumbnail_order)),
      thumbnails(std::move(other.thumbnails)), device(other.device),
      zoom(other.zoom), canvas_size(other.canvas_size), pan(other.pan),
      pending_index(other.pending_index),
      last_drawn_index(other.last_drawn_index), editor(other.editor) {
  other.device = nullptr;
  other.index = 0;
  other.size = 0;
  other.pending_index = -1;
  other.last_drawn_index = -1;
}

ImageManager &ImageManager::operator=(ImageManager &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  clear_current_image();
  clear_thumbnails();

  index = other.index;
  size = other.size;
  image_folder_ = std::move(other.image_folder_);
  current_image_ = std::move(other.current_image_);
  image_names = std::move(other.image_names);
  thumbnail_order = std::move(other.thumbnail_order);
  thumbnails = std::move(other.thumbnails);
  device = other.device;
  zoom = other.zoom;
  canvas_size = other.canvas_size;
  pan = other.pan;
  pending_index = other.pending_index;
  last_drawn_index = other.last_drawn_index;

  other.device = nullptr;
  other.index = 0;
  other.size = 0;
  other.pending_index = -1;
  other.last_drawn_index = -1;
  return *this;
}

void ImageManager::clear_current_image() { current_image_.reset(); }

void ImageManager::clear_thumbnails() {
  if (device == nullptr) {
    thumbnails.clear();
    return;
  }

  for (const auto &[name, thumbnail] : thumbnails) {
    (void)name;
    if (thumbnail.texture != nullptr) {
      SDL_ReleaseGPUTexture(device, thumbnail.texture);
    }
  }
  thumbnails.clear();
}

void ImageManager::load_folder(const std::filesystem::path &folder) {
#ifdef TRACY_ENABLE
  ZoneScopedN("ImageManager::load_folder");
#endif
  image_names.clear();
  if (!std::filesystem::exists(folder) ||
      !std::filesystem::is_directory(folder)) {
    std::cerr << "Image folder is missing or invalid: " << folder.string()
              << std::endl;
    size = 0;
    return;
  }

  SDL_EnumerateDirectory(folder.string().c_str(), enumerate_cb, &image_names);
  std::sort(image_names.begin(), image_names.end());
  size = static_cast<int>(image_names.size());
}

void ImageManager::reset_view_to_image() {
  const Image *image = current_image_.get();
  if (image == nullptr || !image->is_valid()) {
    zoom = 0.0f;
    pan = ImVec2(0.0f, 0.0f);
    return;
  }

  if (canvas_size.x > 0.0f && canvas_size.y > 0.0f) {
    zoom =
        std::min(canvas_size.x / image->width, canvas_size.y / image->height);
  } else {
    zoom = 0.0f;
  }
  pan = ImVec2(0.0f, 0.0f);
}

void ImageEditor::reset_view_to_image() {
  if (preview_texture == nullptr) {
    zoom = 1.0f;
    pan = ImVec2(0.0f, 0.0f);
    return;
  }

  if (canvas_size.x > 0.0f && canvas_size.y > 0.0f) {
    zoom = std::min(canvas_size.x / image_width, canvas_size.y / image_height);
  } else {
    zoom = 1.0f;
  }
  pan = ImVec2(0.0f, 0.0f);
}

void ImageManager::queue_image_by_index(int next_index) {
  if (next_index < 0 || next_index >= static_cast<int>(image_names.size())) {
    return;
  }
  pending_index = next_index;
}

void ImageManager::apply_pending_selection() {
  if (pending_index < 0) {
    return;
  }
  index = pending_index;
  pending_index = -1;
  load_image();
}

Image *ImageManager::load_image() {
#ifdef TRACY_ENABLE
  ZoneScopedN("ImageManager::load_image");
#endif
  if (image_names.empty() || index < 0 ||
      index >= static_cast<int>(image_names.size())) {
    clear_current_image();
    return nullptr;
  }

  auto next_image = std::make_unique<Image>(device, image_names[index]);
  if (!next_image->is_valid()) {
    clear_current_image();
    return nullptr;
  }

  selection_storage.Clear();

  current_image_ = std::move(next_image);
  reset_view_to_image();
  return current_image_.get();
}

Image *ImageManager::load_next() {
  if (image_names.empty()) {
    return nullptr;
  }
  index = index >= static_cast<int>(image_names.size()) - 1 ? 0 : index + 1;
  return load_image();
}

Image *ImageManager::load_previous() {
  if (image_names.empty()) {
    return nullptr;
  }
  index = index <= 0 ? static_cast<int>(image_names.size()) - 1 : index - 1;
  return load_image();
}

bool ImageManager::select_image_by_name(const std::string &name) {
  const auto it = std::find(image_names.begin(), image_names.end(), name);
  if (it == image_names.end()) {
    return false;
  }
  index = static_cast<int>(it - image_names.begin());
  return load_image() != nullptr;
}

const std::vector<std::string> &ImageManager::get_thumbnail_order() const {
  return thumbnail_order;
}

const Thumbnail *ImageManager::get_thumbnail(const std::string &name) const {
  const auto it = thumbnails.find(name);
  return it == thumbnails.end() ? nullptr : &it->second;
}

int ImageManager::get_image_index(const std::string &name) const {
  const auto it = std::find(image_names.begin(), image_names.end(), name);
  return it == image_names.end() ? -1
                                 : static_cast<int>(it - image_names.begin());
}

const Image *ImageManager::get_current_image() const {
  return current_image_.get();
}

const std::filesystem::path *ImageManager::current_image_path() const {
  return current_image_ == nullptr ? nullptr : &current_image_->filename;
}

bool ImageManager::has_images() const { return !image_names.empty(); }

const std::filesystem::path &ImageManager::folder() const {
  return image_folder_;
}

void ImageEditor::load_path(std::filesystem::path path) {
#ifdef TRACY_ENABLE
  ZoneScopedN("load_path");
#endif
  image_path = std::move(path);
  int src_w = 0;
  int src_h = 0;
  unsigned char *src = load_texture_data_from_file(image_path, &src_w, &src_h);

  // constexpr int dst_h = 1024;
  // const float factor = static_cast<float>(dst_h) / src_h;
  // const int dst_w = std::max(1, static_cast<int>(src_w * factor));

  // unsigned char *dst = resize_image_rgba8(src, src_w, src_h, dst_w, dst_h);
  // stbi_image_free(src);
  if (image_src != nullptr) {
    IM_FREE(image_src);
  }
  // image_src = dst;
  // current_mip_level_width = dst_w;
  // current_mip_level_height = dst_h;
  image_src = src;
  image_width = src_w;
  image_height = src_h;

  roi = {0, 0, image_width, image_height};
  prepare_gegl_graph();
  // SDL_GPUTexture *texture = nullptr;
  // upload_texture_data_to_gpu(dst, dst_w, dst_h, device, &texture, false);
  // if (preview_texture != nullptr) {
  //   SDL_ReleaseGPUTexture(device, preview_texture);
  // }
  //
  // preview_texture = texture;
  put_render_request();
}
