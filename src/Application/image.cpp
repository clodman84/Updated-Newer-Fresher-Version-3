#include "include/image.h"

#include "SDL3/SDL_gpu.h"
#include "include/gpu_utils.h"
#include "include/stb_image.h"

#include <SDL3/SDL.h>
#include <gegl.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

Image::~Image() {
  destroy_texture();
  destroy_thumbnail();
}

void Image::load_fullres() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Image::load_fullres");
#endif
  unsigned char *image_data =
      load_texture_data_from_file(filename, &width, &height, 1.0);
  if (image_data == nullptr) {
    return;
  }

  if (!upload_texture_data_to_gpu(image_data, width, height, device,
                                  &texture)) {
    texture = nullptr;
    width = 0;
    height = 0;
  }

  free(image_data);
}

void Image::load_halfres() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Image::load_halfres");
#endif
  unsigned char *image_data =
      load_texture_data_from_file(filename, &width, &height, 0.5);
  if (image_data == nullptr) {
    return;
  }

  if (!upload_texture_data_to_gpu(image_data, width, height, device,
                                  &texture)) {
    texture = nullptr;
    width = 0;
    height = 0;
  }

  free(image_data);
}

void Image::load_thumbnail() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Image::load_thumbnail");
#endif
  if (thumbnail_texture != nullptr)
    return;
  int w;
  int h;
  unsigned char *src = load_texture_data_from_file(filename, &w, &h, 0.25);
  if (src == nullptr) {
    return;
  }

  constexpr int dst_h = 200;
  const float factor = static_cast<float>(dst_h) / h;
  const int dst_w = std::max(1, static_cast<int>(w * factor));

  unsigned char *dst = resize_image_rgba8(src, w, h, dst_w, dst_h);

  free(src);

  w = dst_w;
  h = dst_h;

  if (!upload_texture_data_to_gpu(dst, w, h, device, &thumbnail_texture)) {
    texture = nullptr;
    thumb_width = 0;
    thumb_height = 0;
  } else {
    thumb_width = w;
    thumb_height = h;
  }
  free(dst);
}

void Image::destroy_thumbnail() {
  if (thumbnail_texture != nullptr && device != nullptr) {
    SDL_ReleaseGPUTexture(device, thumbnail_texture);
    thumbnail_texture = nullptr;
  }
}

void Image::destroy_texture() {
  if (texture != nullptr && device != nullptr) {
    SDL_ReleaseGPUTexture(device, texture);
    texture = nullptr;
  }
}

bool Image::is_valid() const {
  return texture != nullptr && width > 0 && height > 0;
}
