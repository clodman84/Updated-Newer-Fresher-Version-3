#include "image_editor.h"
#include "stb_image.h"
#include <gegl.h>
#include <iostream>

bool upload_texture_data_to_gpu(unsigned char *image_data, int width,
                                int height, SDL_GPUDevice *device,
                                SDL_GPUTexture **out_texture,
                                bool free_with_stbi = true) {

  // I will refactor this shit out later, I am in a rush right now
  // Ideally there should be a separate header that defines the
  // load_texture_data_from_file and upload_texture_data_to_gpu
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

ImageEditor::~ImageEditor() {
  if (preview_texture != nullptr)
    SDL_ReleaseGPUTexture(device, preview_texture);
}

void ImageEditor::prepare_gegl_graph() {
  // Clean up previous graph and buffer
  if (graph != nullptr) {
    g_object_unref(graph);
    graph = nullptr;
    sink = nullptr;
    source = nullptr;
  }
  if (image_buffer != nullptr) {
    g_object_unref(image_buffer);
    image_buffer = nullptr;
  }

  // Wrap the raw RGBA pixels into a GeglBuffer
  GeglRectangle extent = {0, 0, width, height};
  image_buffer = gegl_buffer_new(&extent, babl_format("R'G'B'A u8"));

  gegl_buffer_set(image_buffer, &extent,
                  0, // mip level 0
                  babl_format("R'G'B'A u8"), image_src, GEGL_AUTO_ROWSTRIDE);

  // Build graph: buffer-source → brightness-contrast → nop(sink)
  graph = gegl_node_new();
  source = gegl_node_new_child(graph, "operation", "gegl:buffer-source",
                               "buffer", image_buffer, NULL);

  sink = gegl_node_new_child(graph, "operation", "gegl:nop", NULL);

  gegl_node_link_many(source, sink, NULL);
}

void ImageEditor::apply_gegl_texture() {
  if (sink == nullptr)
    return;

  GeglRectangle roi = {0, 0, width, height};
  const size_t buf_size = static_cast<size_t>(width) * height * 4;
  unsigned char *pixels = static_cast<unsigned char *>(IM_ALLOC(buf_size));

  gegl_node_blit(sink,
                 1.0, // scale — 1:1 since you already downsampled via stbir
                 &roi, babl_format("R'G'B'A u8"), pixels, GEGL_AUTO_ROWSTRIDE,
                 GEGL_BLIT_DEFAULT);

  SDL_GPUTexture *texture = nullptr;
  upload_texture_data_to_gpu(pixels, width, height, device, &texture, false);
  IM_FREE(pixels);

  if (preview_texture != nullptr) {
    SDL_ReleaseGPUTexture(device, preview_texture);
  }
  preview_texture = texture;
}

bool DrawGeglBrightnessContrastNode(BrightnessContrastState &s) {
  bool changed = false;
  if (!ImGui::TreeNodeEx("Brightness / Contrast",
                         ImGuiTreeNodeFlags_DefaultOpen |
                             ImGuiTreeNodeFlags_SpanAvailWidth))
    return false;
  float c = (float)s.contrast;
  if (ImGui::SliderFloat("Contrast", &c, 0.0f, 2.0f, "%.3f")) {
    changed = true;
  }
  float b = (float)s.brightness;
  if (ImGui::SliderFloat("Brightness", &b, -1.0f, 1.0f, "%.3f")) {
    changed = true;
  }
  ImGui::TreePop();
  return changed;
}

void ImageEditor::render_controls() {
  DrawGeglBrightnessContrastNode(brightness_contrast_state);
}
