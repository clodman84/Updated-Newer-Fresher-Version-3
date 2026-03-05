#include "SDL3/SDL_gpu.h"
#include <string>
#include <vector>
#define _CRT_SECURE_NO_WARNINGS
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "application.h"
#include "imgui.h"
#include "stb_image.h"
#include "stb_image_resize2.h"
#include <SDL3/SDL.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

unsigned char *LoadTextureDataFromFile(const char *file_name, int *width,
                                       int *height) {

#ifdef TRACY_ENABLE
  ZoneScopedN("LoadTextureDataFromFile");
#endif
  FILE *f = fopen(file_name, "rb");
  if (f == NULL)
    return nullptr;
  fseek(f, 0, SEEK_END);
  size_t file_size = (size_t)ftell(f);
  if (file_size == -1)
    return nullptr;
  fseek(f, 0, SEEK_SET);
  void *file_data = IM_ALLOC(file_size);
  fread(file_data, 1, file_size, f);
  fclose(f);

  int data_size = static_cast<int>(file_size);
  unsigned char *image_data = stbi_load_from_memory(
      (const unsigned char *)file_data, data_size, width, height, NULL, 4);
  IM_FREE(file_data);
  return image_data;
}

bool UploadTextureDataToGPU(unsigned char *image_data, int width, int height,
                            SDL_GPUDevice *device,
                            SDL_GPUTexture **out_texture) {

#ifdef TRACY_ENABLE
  ZoneScopedN("UploadDataToGPU");
#endif

  // Create texture
  SDL_GPUTextureCreateInfo texture_info = {};
  texture_info.type = SDL_GPU_TEXTURETYPE_2D;
  texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  texture_info.width = width;
  texture_info.height = height;
  texture_info.layer_count_or_depth = 1;
  texture_info.num_levels = 1;
  texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;

  SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &texture_info);

  // Create transfer buffer
  // FIXME: A real engine would likely keep one around, see what the SDL_GPU
  // backend is doing.
  SDL_GPUTransferBufferCreateInfo transferbuffer_info = {};
  transferbuffer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transferbuffer_info.size = width * height * 4;
  SDL_GPUTransferBuffer *transferbuffer =
      SDL_CreateGPUTransferBuffer(device, &transferbuffer_info);
  IM_ASSERT(transferbuffer != NULL);

  uint32_t upload_pitch = width * 4;
  void *texture_ptr = SDL_MapGPUTransferBuffer(device, transferbuffer, true);
  for (int y = 0; y < height; y++)
    memcpy((void *)((uintptr_t)texture_ptr + y * upload_pitch),
           image_data + y * upload_pitch, upload_pitch);
  SDL_UnmapGPUTransferBuffer(device, transferbuffer);

  SDL_GPUTextureTransferInfo transfer_info = {};
  transfer_info.offset = 0;
  transfer_info.transfer_buffer = transferbuffer;

  SDL_GPUTextureRegion texture_region = {};
  texture_region.texture = texture;
  texture_region.x = (Uint32)0;
  texture_region.y = (Uint32)0;
  texture_region.w = (Uint32)width;
  texture_region.h = (Uint32)height;
  texture_region.d = 1;

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
  SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
  SDL_UploadToGPUTexture(copy_pass, &transfer_info, &texture_region, false);
  SDL_EndGPUCopyPass(copy_pass);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(device, transferbuffer);

  stbi_image_free(image_data);
  *out_texture = texture;
  return true;
}

SDL_GPUTexture *LoadThumbnailFromFile(const char *file_name,
                                      SDL_GPUDevice *device,
                                      float factor = 0.01f) {
  int width;
  int height;

  unsigned char *image_data =
      LoadTextureDataFromFile(file_name, &width, &height);

  int out_width = width * factor;
  int out_height = height * factor;

  unsigned char *out_data =
      (unsigned char *)IM_ALLOC(out_height * out_width * 4);

  stbir_resize_uint8_linear(image_data, width, height, 0, out_data, out_width,
                            out_height, 0, STBIR_RGBA);
  stbi_image_free(image_data);
  SDL_GPUTexture *texture;
  UploadTextureDataToGPU(out_data, out_width, out_height, device, &texture);
  return texture;
}

Image::Image(SDL_GPUDevice *device, const char *filename)

    : device(device), texture(nullptr), filename(filename) {

#ifdef TRACY_ENABLE
  ZoneScopedN("ImageMaking");
#endif

  unsigned char *image_data =
      LoadTextureDataFromFile(filename, &width, &height);
  if (!image_data) {
    texture = nullptr;
    return;
  }
  bool ok = UploadTextureDataToGPU(image_data, width, height, device, &texture);
  if (!ok) {
    texture = nullptr;
  }
}

Image::~Image() {
  if (texture)
    SDL_ReleaseGPUTexture(device, texture);
}

static bool is_image_file(const char *name) {
  const char *ext = SDL_strrchr(name, '.');
  if (!ext)
    return false;
  return SDL_strcasecmp(ext, ".png") == 0 || SDL_strcasecmp(ext, ".jpg") == 0 ||
         SDL_strcasecmp(ext, ".jpeg") == 0 ||
         SDL_strcasecmp(ext, ".bmp") == 0 || SDL_strcasecmp(ext, ".tga") == 0;
}

static SDL_EnumerationResult enumerate_cb(void *userdata, const char *dirname,
                                          const char *fname) {
  if (fname[0] == '.')
    return SDL_ENUM_CONTINUE;
  if (!is_image_file(fname))
    return SDL_ENUM_CONTINUE;

  auto *image_names = (std::vector<std::string> *)userdata;
  image_names->push_back(std::string(dirname) + fname);
  return SDL_ENUM_CONTINUE;
}

void ImageManager::load_thumbnails() {
  for (const auto &file_name : image_names) {
    SDL_Log("Making Thumbnail: %s", file_name.c_str());
    SDL_GPUTexture *texture = LoadThumbnailFromFile(file_name.c_str(), device);
    thumbnails[file_name] = texture;
  }
}

ImageManager::ImageManager(SDL_GPUDevice *device, const char *imageFolder)
    : device(device), index(0), current_image(nullptr),
      imageFolder(imageFolder) {
  load_folder(imageFolder);
}

ImageManager::~ImageManager() {
  delete current_image;
  for (const auto &val : thumbnails) {
    SDL_ReleaseGPUTexture(device, val.second);
  }
}

void ImageManager::load_folder(const char *folder) {
  SDL_EnumerateDirectory(folder, enumerate_cb, &image_names);
  size = image_names.size();
}

Image *ImageManager::load_image() {
  if (image_names.empty() || index < 0 || index >= (int)image_names.size())
    return nullptr;

  delete current_image;
  current_image = new Image(device, image_names[index].c_str());

  if (!current_image->texture) {
    delete current_image;
    current_image = nullptr;
    return nullptr;
  }

  return current_image;
}

Image *ImageManager::load_next() {
  if (index == image_names.size() - 1)
    index = 0;
  else
    index += 1;
  return load_image();
}

Image *ImageManager::load_previous() {
  if (index == 0)
    index = image_names.size() - 1;
  else
    index -= 1;
  return load_image();
}

inline ImVec2 operator+(const ImVec2 &a, const ImVec2 &b) {
  return ImVec2(a.x + b.x, a.y + b.y);
}

inline ImVec2 operator-(const ImVec2 &a, const ImVec2 &b) {
  return ImVec2(a.x - b.x, a.y - b.y);
}

inline ImVec2 operator*(const ImVec2 &a, float s) {
  return ImVec2(a.x * s, a.y * s);
}

inline ImVec2 operator/(const ImVec2 &a, float s) {
  return ImVec2(a.x / s, a.y / s);
}

inline ImVec2 &operator+=(ImVec2 &a, const ImVec2 &b) {
  a.x += b.x;
  a.y += b.y;
  return a;
}

inline ImVec2 &operator-=(ImVec2 &a, const ImVec2 &b) {
  a.x -= b.x;
  a.y -= b.y;
  return a;
}

void ImageManager::draw_manager(ImGuiIO *io) {
  ImGui::BeginChild(imageFolder);
  if (ImGui::SliderInt("##", &index, 0, size - 1, "%d",
                       ImGuiSliderFlags_AlwaysClamp)) {
    load_image();
  };
  ImTextureRef texture_id = current_image->texture;

  ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
  ImVec2 canvas_size = ImGui::GetContentRegionAvail();

  float base_width = current_image->width;
  float base_height = current_image->height;

  ImGui::InvisibleButton("canvas", canvas_size,
                         ImGuiButtonFlags_MouseButtonLeft);

  bool hovered = ImGui::IsItemHovered();
  bool active = ImGui::IsItemActive();

  if (hovered && io->MouseWheel != 0.0f) {
    float old_zoom = zoom;
    zoom *= powf(1.1f, io->MouseWheel);

    if (zoom < 0.1f)
      zoom = 0.1f;
    if (zoom > 20.0f)
      zoom = 20.0f;

    // Zoom toward mouse
    ImVec2 mouse = io->MousePos;
    ImVec2 mouse_local;
    mouse_local.x = mouse.x - canvas_pos.x - pan.x;
    mouse_local.y = mouse.y - canvas_pos.y - pan.y;

    float scale = zoom / old_zoom - 1.0f;
    pan.x -= mouse_local.x * scale;
    pan.y -= mouse_local.y * scale;
  }

  if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    pan.x += io->MouseDelta.x;
    pan.y += io->MouseDelta.y;
  }

  ImVec2 image_size;
  image_size.x = base_width * zoom;
  image_size.y = base_height * zoom;

  if (image_size.x <= canvas_size.x) {
    // center horizontally
    pan.x = (canvas_size.x - image_size.x) * 0.5f;
  } else {
    float min_x = canvas_size.x - image_size.x;
    float max_x = 0.0f;
    if (pan.x < min_x)
      pan.x = min_x;
    if (pan.x > max_x)
      pan.x = max_x;
  }

  if (image_size.y <= canvas_size.y) {
    // center vertically
    pan.y = (canvas_size.y - image_size.y) * 0.5f;
  } else {
    float min_y = canvas_size.y - image_size.y;
    float max_y = 0.0f;
    if (pan.y < min_y)
      pan.y = min_y;
    if (pan.y > max_y)
      pan.y = max_y;
  }

  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  draw_list->PushClipRect(
      canvas_pos,
      ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), true);

  ImVec2 image_pos;
  image_pos.x = canvas_pos.x + pan.x;
  image_pos.y = canvas_pos.y + pan.y;

  draw_list->AddImage(
      texture_id, image_pos,
      ImVec2(image_pos.x + image_size.x, image_pos.y + image_size.y),
      ImVec2(0, 0), ImVec2(1, 1));

  draw_list->PopClipRect();

  ImGui::EndChild();
}
