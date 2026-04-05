#include "image_editor.h"
#include "gpu_utils.h"
#include "stb_image.h"
#include <gegl.h>
#include <iostream>

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
