#include "application.h"

#include "SDL3/SDL_gpu.h"
#include "imgui.h"

#define _CRT_SECURE_NO_WARNINGS
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"
#include "stb_truetype.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

namespace {

unsigned char *
load_texture_data_from_file(const std::filesystem::path &file_name, int *width,
                            int *height) {
#ifdef TRACY_ENABLE
  ZoneScopedN("load_texture_data_from_file");
#endif
  FILE *file = fopen(file_name.string().c_str(), "rb");
  if (file == nullptr) {
    std::cerr << "Failed to open image file: " << file_name.string()
              << std::endl;
    return nullptr;
  }
  fseek(file, 0, SEEK_END);
  const long raw_size = ftell(file);
  if (raw_size <= 0) {
    fclose(file);
    std::cerr << "Image file is empty: " << file_name.string() << std::endl;
    return nullptr;
  }
  fseek(file, 0, SEEK_SET);
  const size_t file_size = static_cast<size_t>(raw_size);
  void *file_data = IM_ALLOC(file_size);
  const size_t bytes_read = fread(file_data, 1, file_size, file);
  if (bytes_read != file_size) {
    fclose(file);
    IM_FREE(file_data);
    std::cerr << "Failed to read image file: " << file_name.string()
              << std::endl;
    return nullptr;
  }
  fclose(file);

  unsigned char *image_data = stbi_load_from_memory(
      static_cast<const unsigned char *>(file_data),
      static_cast<int>(file_size), width, height, nullptr, 4);
  IM_FREE(file_data);

  if (image_data == nullptr) {
    std::cerr << "Failed to decode image: " << file_name.string() << std::endl;
    return nullptr;
  }

  // Parse EXIF orientation directly from the raw file bytes.
  // Only JPEG (FF D8) files carry EXIF; skip silently for everything else.
  int orientation = 1; // default: normal
  {
    // Re-read just enough bytes to locate the EXIF APP1 marker.
    // JPEG layout: FF D8  [FF Ex marker, 2-byte length, data] ...
    // We only need the first APP1 block which is almost always ≤ 64 KB.
    FILE *exif_file = fopen(file_name.string().c_str(), "rb");
    if (exif_file) {
      // Read up to 65536 bytes — enough to cover the APP1 segment.
      constexpr size_t EXIF_BUF = 65536;
      std::vector<unsigned char> buf(EXIF_BUF);
      const size_t n = fread(buf.data(), 1, EXIF_BUF, exif_file);
      fclose(exif_file);

      // Verify JPEG SOI marker.
      if (n > 4 && buf[0] == 0xFF && buf[1] == 0xD8) {
        size_t pos = 2;
        while (pos + 4 <= n) {
          if (buf[pos] != 0xFF)
            break; // lost sync
          const unsigned char marker = buf[pos + 1];
          const size_t seg_len =
              (static_cast<size_t>(buf[pos + 2]) << 8) | buf[pos + 3];
          // APP1 marker (FF E1) with "Exif\0\0" header.
          if (marker == 0xE1 && pos + 10 <= n && buf[pos + 4] == 'E' &&
              buf[pos + 5] == 'x' && buf[pos + 6] == 'i' &&
              buf[pos + 7] == 'f' && buf[pos + 8] == 0x00 &&
              buf[pos + 9] == 0x00) {
            // TIFF header starts at pos+10.
            const size_t tiff_base = pos + 10;
            if (tiff_base + 8 > n)
              break;

            // Determine byte order: "II" = little-endian, "MM" = big-endian.
            const bool little_endian =
                buf[tiff_base] == 'I' && buf[tiff_base + 1] == 'I';

            auto read16 = [&](size_t offset) -> uint16_t {
              if (offset + 2 > n)
                return 0;
              return little_endian
                         ? (static_cast<uint16_t>(buf[offset]) |
                            (static_cast<uint16_t>(buf[offset + 1]) << 8))
                         : (static_cast<uint16_t>(buf[offset + 1]) |
                            (static_cast<uint16_t>(buf[offset]) << 8));
            };
            auto read32 = [&](size_t offset) -> uint32_t {
              if (offset + 4 > n)
                return 0;
              return little_endian
                         ? (static_cast<uint32_t>(buf[offset]) |
                            (static_cast<uint32_t>(buf[offset + 1]) << 8) |
                            (static_cast<uint32_t>(buf[offset + 2]) << 16) |
                            (static_cast<uint32_t>(buf[offset + 3]) << 24))
                         : (static_cast<uint32_t>(buf[offset + 3]) |
                            (static_cast<uint32_t>(buf[offset + 2]) << 8) |
                            (static_cast<uint32_t>(buf[offset + 1]) << 16) |
                            (static_cast<uint32_t>(buf[offset]) << 24));
            };

            // IFD0 offset is at tiff_base+4.
            const uint32_t ifd_offset = read32(tiff_base + 4);
            const size_t ifd_pos = tiff_base + ifd_offset;
            if (ifd_pos + 2 > n)
              break;

            const uint16_t entry_count = read16(ifd_pos);
            for (uint16_t i = 0; i < entry_count; ++i) {
              const size_t entry_pos = ifd_pos + 2 + i * 12;
              if (entry_pos + 12 > n)
                break;
              const uint16_t tag = read16(entry_pos);
              if (tag == 0x0112) { // Orientation tag
                orientation = static_cast<int>(read16(entry_pos + 8));
                break;
              }
            }
            break; // Done with APP1.
          }
          // Skip to next marker (length field includes its own 2 bytes).
          pos += 2 + seg_len;
        }
      }
    }
  }

  // Apply orientation transform in-place using a temporary buffer.
  // EXIF orientations 2–8 require flip and/or rotation.
  // orientations 1 → no-op; 2 → flip-H; 3 → rotate 180; 4 → flip-V;
  //              5 → transpose; 6 → rotate 90 CW; 7 → transverse;
  //              8 → rotate 90 CCW
  if (orientation != 1 && orientation >= 1 && orientation <= 8) {
    const int w = *width;
    const int h = *height;
    constexpr int CH = 4; // RGBA

    // Helper: swap two pixels.
    auto px = [&](unsigned char *data, int x, int y) -> unsigned char * {
      return data + (y * w + x) * CH;
    };

    // In-place horizontal flip.
    auto flip_h = [&]() {
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w / 2; ++x)
          for (int c = 0; c < CH; ++c)
            std::swap(px(image_data, x, y)[c], px(image_data, w - 1 - x, y)[c]);
    };

    // In-place vertical flip.
    auto flip_v = [&]() {
      for (int y = 0; y < h / 2; ++y)
        for (int x = 0; x < w; ++x)
          for (int c = 0; c < CH; ++c)
            std::swap(px(image_data, x, y)[c], px(image_data, x, h - 1 - y)[c]);
    };

    // Rotation / transpose requires a new buffer (dimensions may change).
    auto rotate_90_cw = [&]() {
      // Output is h×w (new_w = h, new_h = w).
      unsigned char *tmp = static_cast<unsigned char *>(
          IM_ALLOC(static_cast<size_t>(w) * h * CH));
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          for (int c = 0; c < CH; ++c)
            tmp[(x * h + (h - 1 - y)) * CH + c] =
                image_data[(y * w + x) * CH + c];
      stbi_image_free(image_data);
      image_data = tmp;
      *width = h;
      *height = w;
    };

    auto rotate_90_ccw = [&]() {
      unsigned char *tmp = static_cast<unsigned char *>(
          IM_ALLOC(static_cast<size_t>(w) * h * CH));
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          for (int c = 0; c < CH; ++c)
            tmp[((w - 1 - x) * h + y) * CH + c] =
                image_data[(y * w + x) * CH + c];
      stbi_image_free(image_data);
      image_data = tmp;
      *width = h;
      *height = w;
    };

    auto transpose = [&]() {
      // Reflect across main diagonal: (x,y) → (y,x), output is h×w.
      unsigned char *tmp = static_cast<unsigned char *>(
          IM_ALLOC(static_cast<size_t>(w) * h * CH));
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          for (int c = 0; c < CH; ++c)
            tmp[(x * h + y) * CH + c] = image_data[(y * w + x) * CH + c];
      stbi_image_free(image_data);
      image_data = tmp;
      *width = h;
      *height = w;
    };

    auto transverse = [&]() {
      // Reflect across anti-diagonal: (x,y) → (w-1-y, h-1-x).
      unsigned char *tmp = static_cast<unsigned char *>(
          IM_ALLOC(static_cast<size_t>(w) * h * CH));
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          for (int c = 0; c < CH; ++c)
            tmp[((w - 1 - x) * h + (h - 1 - y)) * CH + c] =
                image_data[(y * w + x) * CH + c];
      stbi_image_free(image_data);
      image_data = tmp;
      *width = h;
      *height = w;
    };

    switch (orientation) {
    case 2:
      flip_h();
      break;
    case 3:
      flip_h();
      flip_v();
      break; // rotate 180
    case 4:
      flip_v();
      break;
    case 5:
      transpose();
      break;
    case 6:
      rotate_90_cw();
      break;
    case 7:
      transverse();
      break;
    case 8:
      rotate_90_ccw();
      break;
    default:
      break;
    }
  }

  return image_data;
}

bool upload_texture_data_to_gpu(unsigned char *image_data, int width,
                                int height, SDL_GPUDevice *device,
                                SDL_GPUTexture **out_texture,
                                bool free_with_stbi = true) {
#ifdef TRACY_ENABLE
  ZoneScopedN("upload_texture_data_to_gpu");
#endif
  if (image_data == nullptr || width <= 0 || height <= 0 || device == nullptr ||
      out_texture == nullptr) {
    return false;
  }

  SDL_GPUTextureCreateInfo texture_info = {};
  texture_info.type = SDL_GPU_TEXTURETYPE_2D;
  texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  texture_info.width = static_cast<Uint32>(width);
  texture_info.height = static_cast<Uint32>(height);
  texture_info.layer_count_or_depth = 1;
  texture_info.num_levels = 1;
  texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;

  SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &texture_info);
  if (texture == nullptr) {
    std::cerr << "Failed to create GPU texture: " << SDL_GetError()
              << std::endl;
    if (free_with_stbi) {
      stbi_image_free(image_data);
    } else {
      IM_FREE(image_data);
    }
    return false;
  }

  SDL_GPUTransferBufferCreateInfo transfer_buffer_info = {};
  transfer_buffer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transfer_buffer_info.size = static_cast<Uint32>(width * height * 4);
  SDL_GPUTransferBuffer *transfer_buffer =
      SDL_CreateGPUTransferBuffer(device, &transfer_buffer_info);
  if (transfer_buffer == nullptr) {
    std::cerr << "Failed to create GPU transfer buffer: " << SDL_GetError()
              << std::endl;
    SDL_ReleaseGPUTexture(device, texture);
    if (free_with_stbi) {
      stbi_image_free(image_data);
    } else {
      IM_FREE(image_data);
    }
    return false;
  }

  const uint32_t upload_pitch = static_cast<uint32_t>(width * 4);
  void *texture_ptr = SDL_MapGPUTransferBuffer(device, transfer_buffer, true);
  for (int y = 0; y < height; ++y) {
    memcpy(static_cast<unsigned char *>(texture_ptr) + y * upload_pitch,
           image_data + y * upload_pitch, upload_pitch);
  }
  SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

  SDL_GPUTextureTransferInfo transfer_info = {};
  transfer_info.offset = 0;
  transfer_info.transfer_buffer = transfer_buffer;

  SDL_GPUTextureRegion texture_region = {};
  texture_region.texture = texture;
  texture_region.x = 0;
  texture_region.y = 0;
  texture_region.w = static_cast<Uint32>(width);
  texture_region.h = static_cast<Uint32>(height);
  texture_region.d = 1;

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
  SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
  SDL_UploadToGPUTexture(copy_pass, &transfer_info, &texture_region, false);
  SDL_EndGPUCopyPass(copy_pass);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);

  if (free_with_stbi) {
    stbi_image_free(image_data);
  } else {
    IM_FREE(image_data);
  }

  *out_texture = texture;
  return true;
}

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

  if (!upload_texture_data_to_gpu(image_data, width, height, device, &texture,
                                  true)) {
    texture = nullptr;
    width = 0;
    height = 0;
  }
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

      unsigned char *dst =
          static_cast<unsigned char *>(IM_ALLOC(dst_w * dst_h * 4));
      stbir_resize_uint8_linear(src, src_w, src_h, 0, dst, dst_w, dst_h, 0,
                                STBIR_RGBA);
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
                                   thumbnail.height, device, &texture, false) &&
        texture != nullptr) {
      thumbnails[thumbnail.file_name] =
          Thumbnail{texture, thumbnail.width, thumbnail.height};
      thumbnail_order.push_back(thumbnail.file_name);
    }
  }
}

ImageManager::ImageManager(SDL_GPUDevice *device,
                           const std::filesystem::path &image_folder)
    : image_folder_(image_folder), device(device) {
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
      last_drawn_index(other.last_drawn_index) {
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
