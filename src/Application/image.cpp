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
  if (texture != nullptr && device != nullptr) {
    SDL_ReleaseGPUTexture(device, texture);
  }
  if (thumbnail_texture != nullptr && device != nullptr) {
    SDL_ReleaseGPUTexture(device, thumbnail_texture);
  }
}

void Image::load_fullres() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Image::load_fullres");
#endif
  destroy_texture();
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
  destroy_texture();
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
  destroy_thumbnail();
  unsigned char *src =
      load_texture_data_from_file(filename, &thumb_width, &thumb_height, 0.25);
  if (src == nullptr) {
    return;
  }

  constexpr int dst_h = 200;
  const float factor = static_cast<float>(dst_h) / thumb_height;
  const int dst_w = std::max(1, static_cast<int>(thumb_width * factor));

  unsigned char *dst =
      resize_image_rgba8(src, thumb_width, thumb_height, dst_w, dst_h);

  free(src);

  thumb_width = dst_w;
  thumb_height = dst_h;

  if (!upload_texture_data_to_gpu(dst, thumb_width, thumb_height, device,
                                  &thumbnail_texture)) {
    texture = nullptr;
    width = 0;
    height = 0;
  }

  free(dst);
}

void Image::destroy_thumbnail() {
  if (thumbnail_texture != nullptr && device != nullptr) {
    SDL_ReleaseGPUTexture(device, thumbnail_texture);
  }
}

void Image::destroy_texture() {
  if (texture != nullptr && device != nullptr) {
    SDL_ReleaseGPUTexture(device, texture);
  }
}

bool Image::is_valid() const {
  return texture != nullptr && width > 0 && height > 0;
}
