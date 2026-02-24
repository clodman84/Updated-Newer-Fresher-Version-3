#include <algorithm>
#include <string>
#include <vector>
#define _CRT_SECURE_NO_WARNINGS
#define STB_IMAGE_IMPLEMENTATION
#include "application.h"
#include "imgui.h"
#include "stb_image.h"
#include <SDL3/SDL.h>

bool LoadTextureFromMemory(const void *data, size_t data_size,
                           SDL_GPUDevice *device, SDL_GPUTexture **out_texture,
                           int *out_width, int *out_height) {
  // Load from disk into a raw RGBA buffer
  int width = 0;
  int height = 0;
  unsigned char *image_data = stbi_load_from_memory(
      (const unsigned char *)data, (int)data_size, &width, &height, NULL, 4);

  if (image_data == NULL)
    return false;

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
  *out_width = width;
  *out_height = height;
  return true;
}

// Open and read a file, then forward to LoadTextureFromMemory()
bool LoadTextureFromFile(const char *file_name, SDL_GPUDevice *device,
                         SDL_GPUTexture **out_texture, int *out_width,
                         int *out_height) {
  FILE *f = fopen(file_name, "rb");
  if (f == NULL)
    return false;
  fseek(f, 0, SEEK_END);
  size_t file_size = (size_t)ftell(f);
  if (file_size == -1)
    return false;
  fseek(f, 0, SEEK_SET);
  void *file_data = IM_ALLOC(file_size);
  fread(file_data, 1, file_size, f);
  fclose(f);
  bool ret = LoadTextureFromMemory(file_data, file_size, device, out_texture,
                                   out_width, out_height);
  IM_FREE(file_data);
  return ret;
}

Image::Image(SDL_GPUDevice *device, const char *filename)
    : device(device), texture(nullptr), filename(filename) {
  bool ok = LoadTextureFromFile(filename, device, &texture, &width, &height);
  if (!ok)
    texture = nullptr;
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
  auto *paths = (std::vector<std::string> *)userdata;
  paths->push_back(std::string(dirname) + fname);
  return SDL_ENUM_CONTINUE;
}

ImageManager::ImageManager(SDL_GPUDevice *device, const char *imageFolder)
    : device(device), index(-1), current_image(nullptr),
      imageFolder(imageFolder) {
  loadFolder(imageFolder);
}

ImageManager::~ImageManager() { delete current_image; }

void ImageManager::loadFolder(const char *folder) {
  images.clear();
  SDL_EnumerateDirectory(folder, enumerate_cb, &images);
  std::sort(images.begin(), images.end());
  size = images.size();
}

Image *ImageManager::loadImage() {
  if (images.empty() || index < 0 || index >= (int)images.size())
    return nullptr;

  delete current_image;
  current_image = new Image(device, images[index].c_str());

  if (!current_image->texture) {
    delete current_image;
    current_image = nullptr;
    return nullptr;
  }

  return current_image;
}

Image *ImageManager::loadNext() {
  if (index == images.size() - 1)
    index = 0;
  index += 1;
  return loadImage();
}

Image *ImageManager::loadPrevious() {
  if (index == 0)
    index = images.size() - 1;
  index -= 1;
  return loadImage();
}

void ImageManager::drawManager(ImGuiIO *io) {
  // The image manager can be rendered onto the screen for reference
  ImGui::Begin(imageFolder);
  if (ImGui::Button("Previous")) {
    loadPrevious();
  }
  ImGui::SameLine();
  if (ImGui::Button("Next")) {
    loadNext();
  }
  ImGui::SameLine();
  if (ImGui::SliderInt("##", &index, 0, size - 1, "%d")) {
    loadImage();
  };

  ImTextureRef texture_id = current_image->texture;
  float display_width = 720.0f;
  float display_height = 480.0f;

  {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 uv_min = ImVec2(0.0f, 0.0f); // Top-left
    ImVec2 uv_max = ImVec2(1.0f, 1.0f); // Lower-right
    ImGui::PushStyleVar(ImGuiStyleVar_ImageBorderSize,
                        MAX(1.0f, ImGui::GetStyle().ImageBorderSize));
    ImGui::ImageWithBg(texture_id, ImVec2(display_width, display_height),
                       uv_min, uv_max, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    if (ImGui::BeginItemTooltip()) {
      float region_sz = 32.0f;
      float region_x = io->MousePos.x - pos.x - region_sz * 0.5f;
      float region_y = io->MousePos.y - pos.y - region_sz * 0.5f;
      float zoom = 4.0f;
      if (region_x < 0.0f) {
        region_x = 0.0f;
      } else if (region_x > display_width - region_sz) {
        region_x = display_width - region_sz;
      }
      if (region_y < 0.0f) {
        region_y = 0.0f;
      } else if (region_y > display_height - region_sz) {
        region_y = display_height - region_sz;
      }
      ImVec2 uv0 =
          ImVec2((region_x) / display_width, (region_y) / display_height);
      ImVec2 uv1 = ImVec2((region_x + region_sz) / display_width,
                          (region_y + region_sz) / display_height);
      ImGui::ImageWithBg(texture_id, ImVec2(region_sz * zoom, region_sz * zoom),
                         uv0, uv1, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
      ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
  }
  ImGui::End();
}
