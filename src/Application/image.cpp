#include "SDL3/SDL_gpu.h"
#include <algorithm>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#define _CRT_SECURE_NO_WARNINGS
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#include "application.h"
#include "imgui.h"
#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"
#include "stb_truetype.h"
#include <SDL3/SDL.h>
#include <iostream>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

template <typename T> static inline T Clamp(T v, T lo, T hi) {
  return v < lo ? lo : (v > hi ? hi : v);
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

// Loads a file from disk and decodes it with stbi.
// Returns stbi-owned memory -> caller must free with stbi_image_free().
static unsigned char *LoadTextureDataFromFile(const char *file_name, int *width,
                                              int *height) {
#ifdef TRACY_ENABLE
  ZoneScopedN("LoadTextureDataFromFile");
#endif
  FILE *f = fopen(file_name, "rb");
  if (!f)
    return nullptr;

  fseek(f, 0, SEEK_END);
  long raw_size = ftell(f);
  if (raw_size <= 0) {
    fclose(f);
    return nullptr;
  }
  size_t file_size = (size_t)raw_size;
  fseek(f, 0, SEEK_SET);

  void *file_data = IM_ALLOC(file_size);
  fread(file_data, 1, file_size, f);
  fclose(f);

  unsigned char *image_data = stbi_load_from_memory(
      (const unsigned char *)file_data, (int)file_size, width, height, NULL, 4);
  IM_FREE(file_data);
  return image_data;
}

// Uploads pixel data to a new SDL_GPUTexture and frees the pixel buffer.
//   free_with_stbi = true  -> buffer allocated by stbi    -> stbi_image_free()
//   free_with_stbi = false -> buffer allocated by IM_ALLOC -> IM_FREE()
// On success, *out_texture is set and true is returned.
static bool UploadTextureDataToGPU(unsigned char *image_data, int width,
                                   int height, SDL_GPUDevice *device,
                                   SDL_GPUTexture **out_texture,
                                   bool free_with_stbi = true) {
#ifdef TRACY_ENABLE
  ZoneScopedN("UploadDataToGPU");
#endif
  if (!image_data || width <= 0 || height <= 0)
    return false;

  // Create the destination texture
  SDL_GPUTextureCreateInfo texture_info = {};
  texture_info.type = SDL_GPU_TEXTURETYPE_2D;
  texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  texture_info.width = (Uint32)width;
  texture_info.height = (Uint32)height;
  texture_info.layer_count_or_depth = 1;
  texture_info.num_levels = 1;
  texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;

  SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &texture_info);
  if (!texture) {
    if (free_with_stbi)
      stbi_image_free(image_data);
    else
      IM_FREE(image_data);
    return false;
  }

  // Create a transfer buffer and copy pixels into it
  SDL_GPUTransferBufferCreateInfo transferbuffer_info = {};
  transferbuffer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transferbuffer_info.size = (Uint32)(width * height * 4);

  SDL_GPUTransferBuffer *transferbuffer =
      SDL_CreateGPUTransferBuffer(device, &transferbuffer_info);
  IM_ASSERT(transferbuffer != NULL);

  uint32_t upload_pitch = (uint32_t)(width * 4);
  void *texture_ptr = SDL_MapGPUTransferBuffer(device, transferbuffer, true);
  for (int y = 0; y < height; y++)
    memcpy((void *)((uintptr_t)texture_ptr + y * upload_pitch),
           image_data + y * upload_pitch, upload_pitch);
  SDL_UnmapGPUTransferBuffer(device, transferbuffer);

  // Record and submit the upload copy
  SDL_GPUTextureTransferInfo transfer_info = {};
  transfer_info.offset = 0;
  transfer_info.transfer_buffer = transferbuffer;

  SDL_GPUTextureRegion texture_region = {};
  texture_region.texture = texture;
  texture_region.x = 0;
  texture_region.y = 0;
  texture_region.w = (Uint32)width;
  texture_region.h = (Uint32)height;
  texture_region.d = 1;

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
  SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
  SDL_UploadToGPUTexture(copy_pass, &transfer_info, &texture_region, false);
  SDL_EndGPUCopyPass(copy_pass);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(device, transferbuffer);

  // Free CPU-side pixel buffer using the correct allocator
  if (free_with_stbi)
    stbi_image_free(image_data);
  else
    IM_FREE(image_data);

  *out_texture = texture;
  return true;
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
  bool ok = UploadTextureDataToGPU(image_data, width, height, device, &texture,
                                   /*free_with_stbi=*/true);
  if (!ok)
    texture = nullptr;
}

Image::~Image() {
  if (texture)
    SDL_ReleaseGPUTexture(device, texture);
}

Image::Image(Image &&other) noexcept
    : device(other.device), texture(other.texture), filename(other.filename),
      width(other.width), height(other.height) {
  other.texture = nullptr;
}

struct PendingThumbnail {
  std::string file_name;
  unsigned char *pixel_data = nullptr;
  int width = 0;
  int height = 0;
};

void Session::process_pending_image(const PendingImage &p,
                                    std::mutex &write_mutex) {
  int w, h, channels;
  unsigned char *pixels =
      stbi_load(p.source.string().c_str(), &w, &h, &channels, 3);
  if (!pixels)
    return;

  std::vector<unsigned char> font_buf;
  {
    std::ifstream f("./Data/Quantico-Regular.ttf", std::ios::binary);
    font_buf.assign(std::istreambuf_iterator<char>(f), {});
  }

  stbtt_fontinfo font;
  stbtt_InitFont(&font, font_buf.data(), 0);

  float scale = stbtt_ScaleForPixelHeight(&font, h * 0.04f);
  int ascent, descent, line_gap;
  stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);

  int text_w = 0;
  for (const char *c = p.watermark.c_str(); *c; ++c) {
    int advance, lsb;
    stbtt_GetCodepointHMetrics(&font, *c, &advance, &lsb);
    text_w += (int)(advance * scale);
  }

  int text_h = (int)((ascent - descent) * scale);
  int origin_x = w - text_w - 12;
  int origin_y = h - text_h - 12;
  int baseline = (int)(ascent * scale);

  struct Pass {
    int ox, oy;
    unsigned char r, g, b;
  };
  for (auto &pass : {Pass{2, 2, 0, 0, 0}, Pass{0, 0, 255, 255, 0}}) {
    int cursor_x = origin_x + pass.ox;
    int cursor_y = origin_y + pass.oy;
    for (const char *c = p.watermark.c_str(); *c; ++c) {
      int advance, lsb;
      stbtt_GetCodepointHMetrics(&font, *c, &advance, &lsb);

      int x0, y0, x1, y1;
      stbtt_GetCodepointBitmapBox(&font, *c, scale, scale, &x0, &y0, &x1, &y1);
      int glyph_w = x1 - x0, glyph_h = y1 - y0;

      if (glyph_w > 0 && glyph_h > 0) {
        int gx0, gy0;
        unsigned char *bitmap = stbtt_GetCodepointBitmap(
            &font, scale, scale, *c, &glyph_w, &glyph_h, &gx0, &gy0);
        for (int gy = 0; gy < glyph_h; ++gy) {
          for (int gx = 0; gx < glyph_w; ++gx) {
            int px = cursor_x + gx0 + gx;
            int py = cursor_y + baseline + gy0 + gy;
            if (px < 0 || px >= w || py < 0 || py >= h)
              continue;
            if (bitmap[gy * glyph_w + gx] > 128) {
              unsigned char *dst = pixels + (py * w + px) * 3;
              dst[0] = pass.r;
              dst[1] = pass.g;
              dst[2] = pass.b;
            }
          }
        }
        stbtt_FreeBitmap(bitmap, nullptr);
      }
      cursor_x += (int)(advance * scale);
    }
  }

  {
    std::lock_guard<std::mutex> lock(write_mutex);
    std::filesystem::create_directories(p.destination.parent_path());
    stbi_write_jpg(p.destination.string().c_str(), w, h, 3, pixels, 95);
  }
  stbi_image_free(pixels);
  export_progress.fetch_add(1);
}

void Session::export_images() {
  std::string roll = path.filename();

  for (const auto &image : bill) {
    std::cout << image.first << '\n';
    for (const auto &student_id_bill_pairs : image.second) {
      ExportInfo info =
          database->get_export_information_from_id(student_id_bill_pairs.first);
      std::cout << "\tDatabase: " + info.bhawan << " " << info.roomno + '\n';
      for (int i = 1; i <= student_id_bill_pairs.second.count; i++) {
        std::string bruh = roll + "_" + info.bhawan + "_" + info.roomno + "_" +
                           std::to_string(i) + "_" +
                           student_id_bill_pairs.first + ".jpg";
        std::filesystem::path destination =
            std::filesystem::path("./Data/") / roll / bruh;
        std::string watermark = info.bhawan + " " + info.roomno;
        std::cout << "\t" << "destination: " << destination
                  << " | watermark: " << watermark << '\n';
        pending.push_back({image.first, destination, watermark});
      }
    }
  }

  std::mutex write_mutex;
  std::atomic<size_t> next_index{0};
  const size_t thread_count =
      std::min<size_t>(std::thread::hardware_concurrency(), pending.size());
  std::vector<std::thread> workers;

  for (size_t t = 0; t < thread_count; ++t) {
    workers.emplace_back([&]() {
      while (true) {
        size_t i = next_index.fetch_add(1);
        if (i >= pending.size())
          return;
        process_pending_image(pending[i], write_mutex);
      }
    });
  }
  for (auto &t : workers)
    t.join();
}

void ImageManager::load_thumbnails() {
  std::vector<PendingThumbnail> pending(image_names.size());
  std::vector<std::thread> workers;
  workers.reserve(image_names.size());

  for (size_t i = 0; i < image_names.size(); ++i) {
    pending[i].file_name = image_names[i];
    workers.emplace_back([i, &pending]() {
#ifdef TRACY_ENABLE
      ZoneScopedN("ThumbnailWorker");
#endif
      int src_w = 0, src_h = 0;
      unsigned char *src =
          LoadTextureDataFromFile(pending[i].file_name.c_str(), &src_w, &src_h);
      if (!src)
        return;

      int dst_h = 200;
      float factor = (float)dst_h / src_h;
      int dst_w = src_w * factor;

      unsigned char *dst = (unsigned char *)IM_ALLOC(dst_w * dst_h * 4);
      stbir_resize_uint8_linear(src, src_w, src_h, 0, dst, dst_w, dst_h, 0,
                                STBIR_RGBA);
      stbi_image_free(src);

      pending[i].pixel_data = dst;
      pending[i].width = dst_w;
      pending[i].height = dst_h;
    });
  }

  for (auto &t : workers)
    t.join();

  for (auto &p : pending) {
    SDL_Log("Uploading thumbnail to GPU: %s", p.file_name.c_str());
    if (!p.pixel_data)
      continue;

    SDL_GPUTexture *texture = nullptr;
    bool ok = UploadTextureDataToGPU(p.pixel_data, p.width, p.height, device,
                                     &texture, /*free_with_stbi=*/false);
    if (ok && texture) {
      thumbnails[p.file_name] = Thumbnail_T{texture, p.width, p.height};
      thumbnail_order.push_back(p.file_name);
    }
  }
}

ImageManager::ImageManager(SDL_GPUDevice *device, const char *imageFolder)
    : device(device), index(0), size(0), current_image(nullptr),
      imageFolder(imageFolder), pending_index(-1) {
  load_folder(imageFolder);
  load_thumbnails();
  load_image();
}

ImageManager::~ImageManager() {
  delete current_image;
  for (const auto &val : thumbnails)
    SDL_ReleaseGPUTexture(device, val.second.texture);
}

ImageManager::ImageManager(ImageManager &&other) noexcept
    : device(other.device), index(other.index), size(other.size),
      current_image(other.current_image), imageFolder(other.imageFolder),
      image_names(std::move(other.image_names)),
      thumbnail_order(std::move(other.thumbnail_order)),
      thumbnails(std::move(other.thumbnails)), zoom(other.zoom), pan(other.pan),
      pending_index(other.pending_index) {
  other.current_image = nullptr;
  other.thumbnails.clear();
}

ImageManager &ImageManager::operator=(ImageManager &&other) noexcept {
  if (this == &other)
    return *this;

  delete current_image;
  for (const auto &val : thumbnails)
    SDL_ReleaseGPUTexture(device, val.second.texture);

  device = other.device;
  index = other.index;
  size = other.size;
  current_image = other.current_image;
  imageFolder = other.imageFolder;
  image_names = std::move(other.image_names);
  thumbnail_order = std::move(other.thumbnail_order);
  thumbnails = std::move(other.thumbnails);
  zoom = other.zoom;
  pan = other.pan;
  pending_index = other.pending_index;

  other.current_image = nullptr;
  other.thumbnails.clear();
  return *this;
}

void ImageManager::load_folder(const char *folder) {
  SDL_EnumerateDirectory(folder, enumerate_cb, &image_names);
  size = (int)image_names.size();
}

Image *ImageManager::load_image() {
  if (image_names.empty() || index < 0 || index >= (int)image_names.size())
    return nullptr;

  delete current_image;
  current_image = new Image(device, image_names[index].c_str());
  zoom = std::min(canvas_size.x / current_image->width,
                  canvas_size.y / current_image->height);

  if (!current_image->texture) {
    delete current_image;
    current_image = nullptr;
    return nullptr;
  }
  return current_image;
}

Image *ImageManager::load_next() {
  index = (index == (int)image_names.size() - 1) ? 0 : index + 1;
  return load_image();
}

Image *ImageManager::load_previous() {
  index = (index == 0) ? (int)image_names.size() - 1 : index - 1;
  return load_image();
}

void ImageManager::draw_manager(ImGuiIO *io) {

  if (pending_index >= 0) {
    index = pending_index;
    pending_index = -1;
    load_image();
  }

  ImGui::BeginChild("ImagePanel");

  const float carousel_height = 270.0f;

  ImVec2 avail = ImGui::GetContentRegionAvail();

  float top_height = avail.y - carousel_height - 5;

  if (top_height < 0)
    top_height = 0;

  ImGui::BeginChild("TopRegion", ImVec2(0, top_height),
                    ImGuiChildFlags_Borders);

  if (ImGui::BeginTable("MainLayout", 2,
                        ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_SizingStretchProp,
                        ImVec2(0, 0))) {

    ImGui::TableSetupColumn("Viewer", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthStretch, 1.0f);

    ImGui::TableNextRow();

    ImGui::TableNextColumn();

    ImGui::BeginChild("ViewerChild", ImVec2(0, 0));

    if (!current_image || !current_image->texture) {

      // lowkey this will never happen
      ImGui::Text("No image loaded.");

    } else {

      ImTextureRef texture_id = current_image->texture;

      ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
      canvas_size = ImGui::GetContentRegionAvail();

      float base_width = (float)current_image->width;
      float base_height = (float)current_image->height;

      // the image is loaded before canvas is made so this fixes
      // the scaling for that case
      if (zoom == 0.0f)
        zoom = std::min(canvas_size.x / current_image->width,
                        canvas_size.y / current_image->height);

      ImGui::InvisibleButton("canvas", canvas_size,
                             ImGuiButtonFlags_MouseButtonLeft);

      bool hovered = ImGui::IsItemHovered();
      bool active = ImGui::IsItemActive();

      if (hovered && io->MouseWheel != 0.0f) {

        float old_zoom = zoom;

        zoom *= powf(1.1f, io->MouseWheel);
        zoom = Clamp(zoom, 0.1f, 20.0f);

        ImVec2 mouse_local;

        mouse_local.x = io->MousePos.x - canvas_pos.x - pan.x;
        mouse_local.y = io->MousePos.y - canvas_pos.y - pan.y;

        float scale = zoom / old_zoom - 1.0f;

        pan.x -= mouse_local.x * scale;
        pan.y -= mouse_local.y * scale;
      }

      if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        pan.x += io->MouseDelta.x;
        pan.y += io->MouseDelta.y;
      }

      ImVec2 image_size = {base_width * zoom, base_height * zoom};

      if (image_size.x <= canvas_size.x)
        pan.x = (canvas_size.x - image_size.x) * 0.5f;
      else
        pan.x = Clamp(pan.x, canvas_size.x - image_size.x, 0.0f);

      if (image_size.y <= canvas_size.y)
        pan.y = (canvas_size.y - image_size.y) * 0.5f;
      else
        pan.y = Clamp(pan.y, canvas_size.y - image_size.y, 0.0f);

      ImDrawList *draw_list = ImGui::GetWindowDrawList();

      draw_list->PushClipRect(
          canvas_pos,
          ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
          true);

      ImVec2 image_pos = {canvas_pos.x + pan.x, canvas_pos.y + pan.y};

      draw_list->AddImage(
          texture_id, image_pos,
          ImVec2(image_pos.x + image_size.x, image_pos.y + image_size.y),
          ImVec2(0, 0), ImVec2(1, 1));

      draw_list->PopClipRect();
    }

    ImGui::EndChild();

    ImGui::TableNextColumn();

    ImGui::BeginChild("EditingPanel", ImVec2(0, 0));

    ImGui::Text("Editing Panel");

    ImGui::Separator();

    ImGui::TextUnformatted("This is a work in progress :)");
    ImGui::Text("Zoom %.2fx", zoom);

    if (ImGui::Button("Reset View")) {
      zoom = std::min(canvas_size.x / current_image->width,
                      canvas_size.y / current_image->height);
      pan = {0.0f, 0.0f};
    }

    ImGui::EndChild();

    ImGui::EndTable();
  }

  ImGui::EndChild();

  ImGui::BeginChild("Carousel", ImVec2(0, carousel_height),
                    ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_HorizontalScrollbar);

  if (ImGui::IsWindowHovered())
    ImGui::SetScrollX(ImGui::GetScrollX() - io->MouseWheel * 50.0f);

  for (const auto &name : thumbnail_order) {

    auto it = thumbnails.find(name);
    if (it == thumbnails.end())
      continue;

    const Thumbnail_T &thumb = it->second;

    if (!thumb.texture)
      continue;

    bool is_current = (!image_names.empty() && name == image_names[index]);

    if (is_current)
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

    ImTextureRef tid = thumb.texture;

    ImVec2 sz = {(float)thumb.width, (float)thumb.height};

    ImGui::BeginGroup();

    auto fit = std::find(image_names.begin(), image_names.end(), name);

    if (fit != image_names.end()) {

      int frame_number = (int)(fit - image_names.begin()) + 1;

      ImGui::Text("Frame: %d", frame_number);
    }

    if (ImGui::ImageButton(name.c_str(), tid, sz)) {

      if (fit != image_names.end())
        pending_index = (int)(fit - image_names.begin());
    }

    ImGui::EndGroup();

    if (is_current) {

      ImGui::PopStyleColor();

      if (index != last_drawn_index)
        ImGui::SetScrollHereX(0.5f);
    }

    ImGui::SameLine();
  }

  ImGui::EndChild();

  ImGui::EndChild();

  last_drawn_index = index;
}
