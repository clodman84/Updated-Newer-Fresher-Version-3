#include "include/image_editor.h"
#include "SDL3/SDL_log.h"
#include "imgui.h"
#include "include/gpu_utils.h"
#include "include/stb_image.h"
#include <algorithm>
#include <gegl.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

ImageEditor::~ImageEditor() {
  SDL_Log("Cleaning Up Image Editor");
  if (preview_texture != nullptr) {
    SDL_ReleaseGPUTexture(device, preview_texture);
    preview_texture = nullptr;
  }
  cleanup_stale_resources();
  stop_render_thread();

  if (graph != nullptr) {
    g_object_unref(graph);
    graph = nullptr;
  }
  if (image_buffer != nullptr) {
    g_object_unref(image_buffer);
    image_buffer = nullptr;
  }
  if (image_src != nullptr) {
    IM_FREE(image_src);
    image_src = nullptr;
  }
}

void ImageEditor::cleanup_stale_resources() {
  for (auto *tex : textures_to_release) {
    SDL_ReleaseGPUTexture(device, tex);
  }
  textures_to_release.clear();
}

void ImageEditor::remove_effect(EffectType type) {
  auto it = std::find_if(effects.begin(), effects.end(),
                         [type](const Effect &e) { return e.type == type; });

  if (it == effects.end())
    return;

  GeglNode *to_remove = it->node;
  size_t index = std::distance(effects.begin(), it);

  GeglNode *prev = (index == 0) ? source : effects[index - 1].node;
  GeglNode *next =
      (index == effects.size() - 1) ? sink : effects[index + 1].node;

  gegl_node_link(prev, next);
  gegl_node_remove_child(graph, to_remove);
  effects.erase(it);
}

bool ImageEditor::is_effect_active(EffectType type) const {
  return std::any_of(effects.begin(), effects.end(),
                     [type](const Effect &e) { return e.type == type; });
}

void ImageEditor::prepare_gegl_graph() {
  if (graph != nullptr) {
    // Only swap the buffer, touch nothing else
    if (image_buffer != nullptr) {
      g_object_unref(image_buffer);
      image_buffer = nullptr;
    }

    GeglRectangle extent = {0, 0, image_width, image_height};
    image_buffer = gegl_buffer_new(&extent, babl_format("R'G'B'A u8"));
    gegl_buffer_set(image_buffer, &extent, 0, babl_format("R'G'B'A u8"),
                    image_src, GEGL_AUTO_ROWSTRIDE);

    gegl_node_set(source, "buffer", image_buffer, NULL);
    return;
  }

  // First-time initialization only
  GeglRectangle extent = {0, 0, image_width, image_height};
  image_buffer = gegl_buffer_new(&extent, babl_format("R'G'B'A u8"));
  gegl_buffer_set(image_buffer, &extent, 0, babl_format("R'G'B'A u8"),
                  image_src, GEGL_AUTO_ROWSTRIDE);

  graph = gegl_node_new();
  source = gegl_node_new_child(graph, "operation", "gegl:buffer-source",
                               "buffer", image_buffer, NULL);
  sink = gegl_node_new_child(graph, "operation", "gegl:nop", NULL);
  gegl_node_link_many(source, sink, NULL);
}

void ImageEditor::start_render_thread() {
  if (running)
    return;

  running = true;
  has_request = false;

  render_thread = std::thread([this]() {
    while (running) {
      RenderRequest req;
      {
        std::unique_lock<std::mutex> lock(request_mutex);
        request_cv.wait(lock, [this]() { return has_request || !running; });
        if (!running) {
          break;
        }
        req = latest_request;
        has_request = false;
      }
      apply_gegl_texture(req);
    }
    SDL_Log("Bye Bye!");
  });
}

void ImageEditor::stop_render_thread() {
  SDL_Log("Stopping Render Thread");
  if (running) {
    running = false;
    request_cv.notify_one();
    if (render_thread.joinable()) {
      render_thread.join();
    }
  }
}

void ImageEditor::put_render_request() {
  {
    std::lock_guard<std::mutex> lock(request_mutex);
    latest_request.roi = roi;
    latest_request.zoom = zoom;
    has_request = true;
  }
  request_cv.notify_one();
}

void ImageEditor::apply_gegl_texture(RenderRequest req) {
#ifdef TRACY_ENABLE
  ZoneScopedN("apply_gegl_texture");
#endif
  if (req.roi.width <= 0 || req.roi.height <= 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CRITICAL: GEGL ROI is empty!.");
    return;
  }
  if (sink == nullptr)
    return;
  double scale = std::min(zoom, 1.0f);

  GeglRectangle roi = req.roi;
  float zoom = req.zoom;

  int out_w = roi.width * scale;
  int out_h = roi.height * scale;

  size_t buf_size = (size_t)out_w * out_h * 4;
  unsigned char *pixels = (unsigned char *)IM_ALLOC(buf_size);

  GeglRectangle broi = {(int)(roi.x * scale), (int)(roi.y * scale), out_w,
                        out_h};

  gegl_node_blit(sink, scale, &broi, babl_format("R'G'B'A u8"), pixels,
                 GEGL_AUTO_ROWSTRIDE, GEGL_BLIT_DEFAULT);

  SDL_GPUTexture *texture = nullptr;
  if (upload_texture_data_to_gpu(pixels, out_w, out_h, device, &texture)) {
    if (preview_texture != nullptr) {
      textures_to_release.push_back(preview_texture);
    }
    preview_texture = texture;
  } else {
    SDL_Log("Texture upload failed!!!");
  }

  IM_FREE(pixels);
  current_texture_offset_x = roi.x;
  current_texture_offset_y = roi.y;
  current_texture_width = roi.width;
  current_texture_height = roi.height;
}

Effect &ImageEditor::get_or_create_effect(EffectType type) {
  for (auto &effect : effects)
    if (effect.type == type)
      return effect;

  Effect e;
  e.type = type;

  switch (type) {
    // TODO: Add gimp's colour balance tool
  case EffectType::Exposure:
    e.node =
        gegl_node_new_child(graph, "operation", "gegl:exposure", "black-level",
                            (gdouble)exposure_state.black_level, "exposure",
                            (gdouble)exposure_state.exposure, NULL);
    break;
  case EffectType::Levels:
    e.node = gegl_node_new_child(
        graph, "operation", "unfv3:gimp-levels", "in-low",
        (gdouble)levels_state.in_low, "in-high", (gdouble)levels_state.in_high,
        "gamma", (gdouble)levels_state.gamma, "out-low",
        (gdouble)levels_state.out_low, "out-high",
        (gdouble)levels_state.out_high, "in-low-r",
        (gdouble)levels_state.in_low_r, "in-high-r",
        (gdouble)levels_state.in_high_r, "gamma-r",
        (gdouble)levels_state.gamma_r, "out-low-r",
        (gdouble)levels_state.out_low_r, "out-high-r",
        (gdouble)levels_state.out_high_r, "in-low-g",
        (gdouble)levels_state.in_low_g, "in-high-g",
        (gdouble)levels_state.in_high_g, "gamma-g",
        (gdouble)levels_state.gamma_g, "out-low-g",
        (gdouble)levels_state.out_low_g, "out-high-g",
        (gdouble)levels_state.out_high_g, "in-low-b",
        (gdouble)levels_state.in_low_b, "in-high-b",
        (gdouble)levels_state.in_high_b, "gamma-b",
        (gdouble)levels_state.gamma_b, "out-low-b",
        (gdouble)levels_state.out_low_b, "out-high-b",
        (gdouble)levels_state.out_high_b, "in-low-a",
        (gdouble)levels_state.in_low_a, "in-high-a",
        (gdouble)levels_state.in_high_a, "gamma-a",
        (gdouble)levels_state.gamma_a, "out-low-a",
        (gdouble)levels_state.out_low_a, "out-high-a",
        (gdouble)levels_state.out_high_a, NULL);
    break;
  case EffectType::ColorTemperature:
    e.node = gegl_node_new_child(
        graph, "operation", "gegl:color-temperature", "original-temperature",
        (gdouble)color_temperature_state.original_temperature,
        "intended-temperature",
        (gdouble)color_temperature_state.intended_temperature, NULL);
    break;
  case EffectType::HueChroma:
    e.node = gegl_node_new_child(graph, "operation", "gegl:hue-chroma", "hue",
                                 (gdouble)hue_chroma_state.hue, "chroma",
                                 (gdouble)hue_chroma_state.chroma, "lightness",
                                 (gdouble)hue_chroma_state.lightness, NULL);
    break;
  case EffectType::Saturation:
    e.node = gegl_node_new_child(graph, "operation", "gegl:saturation", "scale",
                                 (gdouble)saturation_state.scale, NULL);
    break;
  case EffectType::ColorEnhance:
    e.node =
        gegl_node_new_child(graph, "operation", "unfv3:color-enhance", NULL);
    break;
  case EffectType::Sepia:
    e.node = gegl_node_new_child(graph, "operation", "gegl:sepia", "scale",
                                 (gdouble)sepia_state.scale, NULL);
    break;
  case EffectType::MonoMixer:
    e.node = gegl_node_new_child(
        graph, "operation", "gegl:mono-mixer", "red",
        (gdouble)mono_mixer_state.red, "green", (gdouble)mono_mixer_state.green,
        "blue", (gdouble)mono_mixer_state.blue, "preserve-luminosity",
        (gboolean)mono_mixer_state.preserve_luminosity, NULL);
    break;
  case EffectType::UnsharpMask:
    e.node =
        gegl_node_new_child(graph, "operation", "gegl:unsharp-mask", "std-dev",
                            (gdouble)unsharp_mask_state.std_dev, "scale",
                            (gdouble)unsharp_mask_state.scale, "threshold",
                            (gdouble)unsharp_mask_state.threshold, NULL);
    break;
  case EffectType::NoiseReduction:
    e.node = gegl_node_new_child(graph, "operation", "gegl:noise-reduction",
                                 "iterations",
                                 (gint)noise_reduction_state.iterations, NULL);
    break;
  }

  GeglNode *before_sink = effects.empty() ? source : effects.back().node;

  gegl_node_link(before_sink, e.node);
  gegl_node_link(e.node, sink);

  effects.emplace_back(e);
  return effects.back();
}
