#include "gpu_utils.h"

#include "imgui.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_resize2.h"

#include <iostream>
#include <vector>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

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
                                bool free_with_stbi) {
#ifdef TRACY_ENABLE
  ZoneScopedN("upload_texture_data_to_gpu");
#endif
  auto cleanup = [&]() {
    if (image_data != nullptr) {
      if (free_with_stbi) {
        stbi_image_free(image_data);
      } else {
        IM_FREE(image_data);
      }
      image_data = nullptr;
    }
  };

  if (image_data == nullptr || width <= 0 || height <= 0 || device == nullptr ||
      out_texture == nullptr) {
    cleanup();
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
    cleanup();
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
    cleanup();
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

  cleanup();

  *out_texture = texture;
  return true;
}

unsigned char *resize_image_rgba8(const unsigned char *src_data, int src_w,
                                  int src_h, int dst_w, int dst_h) {
  unsigned char *dst_data =
      static_cast<unsigned char *>(IM_ALLOC(static_cast<size_t>(dst_w) * dst_h * 4));
  if (dst_data == nullptr) {
    return nullptr;
  }
  stbir_resize_uint8_linear(src_data, src_w, src_h, 0, dst_data, dst_w, dst_h, 0,
                            STBIR_RGBA);
  return dst_data;
}
