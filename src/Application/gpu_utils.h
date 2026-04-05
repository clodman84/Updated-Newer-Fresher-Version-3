#ifndef GPU_UTILS_H
#define GPU_UTILS_H

#include <SDL3/SDL.h>
#include <filesystem>

unsigned char *load_texture_data_from_file(const std::filesystem::path &file_name,
                                           int *width, int *height);

bool upload_texture_data_to_gpu(unsigned char *image_data, int width, int height,
                                SDL_GPUDevice *device,
                                SDL_GPUTexture **out_texture,
                                bool free_with_stbi = true);

unsigned char *resize_image_rgba8(const unsigned char *src_data, int src_w,
                                  int src_h, int dst_w, int dst_h);

#endif // GPU_UTILS_H
