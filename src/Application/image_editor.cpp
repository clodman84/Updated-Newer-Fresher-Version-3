#include "image_editor.h"
#include "gegl-node.h"
#include "gegl-types.h"
#include "gpu_utils.h"
#include "imgui.h"
#include "stb_image.h"
#include <gegl.h>

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

Effect &ImageEditor::get_or_create_effect(EffectType type) {
  for (auto &effect : effects)
    if (effect.type == type)
      return effect;

  Effect e;
  e.type = type;

  switch (type) {
  case EffectType::BrightnessContrast:
    e.node = gegl_node_new_child(
        graph, "operation", "gegl:brightness-contrast", "brightness",
        (gdouble)brightness_contrast_state.brightness, "contrast",
        (gdouble)brightness_contrast_state.contrast, NULL);
    break;
  }

  GeglNode *before_sink = effects.empty() ? source : effects.back().node;

  gegl_node_link(before_sink, e.node);
  gegl_node_link(e.node, sink);

  effects.emplace_back(e);
  return effects.back();
}

void ImageEditor::render_controls() {
  if (ImGui::TreeNode("Brightness / Contrast")) {
    bool changed = false;
    static const double brightness_min = -1.0, brightness_max = 1.0;
    static const double contrast_min = 0.0, contrast_max = 2.0;

    changed |= ImGui::SliderScalar("Brightness", ImGuiDataType_Double,
                                   &brightness_contrast_state.brightness,
                                   &brightness_min, &brightness_max, "%.2f");
    changed |= ImGui::SliderScalar("Contrast", ImGuiDataType_Double,
                                   &brightness_contrast_state.contrast,
                                   &contrast_min, &contrast_max, "%.2f");

    if (changed) {
      Effect &e = get_or_create_effect(EffectType::BrightnessContrast);
      gegl_node_set(e.node, "brightness", brightness_contrast_state.brightness,
                    "contrast", brightness_contrast_state.contrast, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }
}
