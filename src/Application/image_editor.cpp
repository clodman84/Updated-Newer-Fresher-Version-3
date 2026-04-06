#include "image_editor.h"
#include "gegl-node.h"
#include "gegl-types.h"
#include "gpu_utils.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "stb_image.h"
#include <algorithm>
#include <cmath>
#include <gegl.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

ImageEditor::~ImageEditor() {
  if (preview_texture != nullptr) {
    SDL_ReleaseGPUTexture(device, preview_texture);
    preview_texture = nullptr;
  }
  cleanup_stale_resources();

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
  effects.clear();

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
#ifdef TRACY_ENABLE
  ZoneScopedN("apply_gegl_texture");
#endif
  if (sink == nullptr)
    return;

  GeglRectangle roi = {0, 0, width, height};
  const size_t buf_size = static_cast<size_t>(width) * height * 4;
  unsigned char *pixels = static_cast<unsigned char *>(IM_ALLOC(buf_size));
  if (pixels == nullptr)
    return;

  gegl_node_blit(sink, 1.0, &roi, babl_format("R'G'B'A u8"), pixels,
                 GEGL_AUTO_ROWSTRIDE, GEGL_BLIT_DEFAULT);

  SDL_GPUTexture *texture = nullptr;
  if (upload_texture_data_to_gpu(pixels, width, height, device, &texture,
                                 false)) {
    if (preview_texture != nullptr) {
      textures_to_release.push_back(preview_texture);
    }
    preview_texture = texture;
  }
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
  case EffectType::ShadowsHighlights:
    e.node = gegl_node_new_child(
        graph, "operation", "gegl:shadows-highlights", "shadows",
        (gdouble)shadows_highlights_state.shadows, "highlights",
        (gdouble)shadows_highlights_state.highlights, "whitepoint",
        (gdouble)shadows_highlights_state.whitepoint, "radius",
        (gdouble)shadows_highlights_state.radius, "compress",
        (gdouble)shadows_highlights_state.compress, "shadows-ccorrect",
        (gdouble)shadows_highlights_state.shadows_ccorrect,
        "highlights-ccorrect",
        (gdouble)shadows_highlights_state.highlights_ccorrect, NULL);
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
        gegl_node_new_child(graph, "operation", "gegl:color-enhance", NULL);
    break;
  case EffectType::StretchContrast:
    e.node =
        gegl_node_new_child(graph, "operation", "gegl:stretch-contrast", NULL);
    break;
  case EffectType::StretchContrastHSV:
    e.node = gegl_node_new_child(graph, "operation",
                                 "gegl:stretch-contrast-hsv", NULL);
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
  case EffectType::SNNMean:
    e.node = gegl_node_new_child(graph, "operation", "gegl:snn-mean", "radius",
                                 (gint)snn_mean_state.radius, "pairs",
                                 (gint)snn_mean_state.pairs, NULL);
    break;
  }

  GeglNode *before_sink = effects.empty() ? source : effects.back().node;

  gegl_node_link(before_sink, e.node);
  gegl_node_link(e.node, sink);

  effects.emplace_back(e);
  return effects.back();
}

static bool DrawLevelsBar(const char *id, double &in_low, double &gamma,
                          double &in_high, double &out_low, double &out_high,
                          ImVec4 color_left, ImVec4 color_right) {
  ImGui::PushID(id);
  ImDrawList *dl = ImGui::GetWindowDrawList();

  float width = ImGui::GetContentRegionAvail().x;
  float height = 16.0f;

  bool changed = false;

  ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImVec2 p1 = ImVec2(p0.x + width, p0.y + height);

  const int STEPS = 64;
  for (int i = 0; i < STEPS; i++) {
    float t0 = (float)i / STEPS;
    float t1 = (float)(i + 1) / STEPS;

    // apply input levels
    auto remap = [&](float t) {
      if (t <= in_low)
        return 0.0f;
      if (t >= in_high)
        return 1.0f;

      float x = (t - in_low) / (in_high - in_low);

      // gamma curve
      float g = (float)(1.0 / gamma);
      x = powf(x, g);

      // output levels
      return (float)(out_low + x * (out_high - out_low));
    };

    float r0 = remap(t0);
    float r1 = remap(t1);

    ImVec4 c0 = ImLerp(color_left, color_right, r0);
    ImVec4 c1 = ImLerp(color_left, color_right, r1);

    float x0 = p0.x + t0 * width;
    float x1 = p0.x + t1 * width;

    dl->AddRectFilledMultiColor(
        ImVec2(x0, p0.y), ImVec2(x1, p1.y), ImGui::ColorConvertFloat4ToU32(c0),
        ImGui::ColorConvertFloat4ToU32(c1), ImGui::ColorConvertFloat4ToU32(c1),
        ImGui::ColorConvertFloat4ToU32(c0));
  }

  auto gamma_to_t = [&](double g) {
    return (float)((log(g) - log(0.1)) / (log(10.0) - log(0.1)));
  };

  auto t_to_gamma = [&](float t) {
    return exp(log(0.1) + t * (log(10.0) - log(0.1)));
  };

  float t_low = (float)in_low;
  float t_high = (float)in_high;
  float t_gamma = gamma_to_t(gamma);

  ImGui::InvisibleButton("input_bar", ImVec2(width, height + 12));
  bool active = ImGui::IsItemActive();

  float mouse_t = (ImGui::GetIO().MousePos.x - p0.x) / width;
  mouse_t = std::clamp(mouse_t, 0.0f, 1.0f);

  float d_low = fabsf(mouse_t - t_low);
  float d_mid = fabsf(mouse_t - t_gamma);
  float d_high = fabsf(mouse_t - t_high);

  enum Handle { LOW, MID, HIGH };
  Handle target = LOW;

  if (d_mid < d_low && d_mid < d_high)
    target = MID;
  else if (d_high < d_low)
    target = HIGH;

  if (active && ImGui::IsMouseDragging(0)) {
    if (target == LOW) {
      in_low = std::min((double)mouse_t, in_high - 0.001);
    } else if (target == HIGH) {
      in_high = std::max((double)mouse_t, in_low + 0.001);
    } else {
      gamma = std::clamp(t_to_gamma(mouse_t), 0.1, 10.0);
    }
    changed = true;
  }

  auto X = [&](float t) { return p0.x + t * width; };

  auto draw_tri_up = [&](float t, ImU32 col) {
    float x = X(t);
    float y = p1.y;
    dl->AddTriangleFilled(ImVec2(x - 6, y + 2), ImVec2(x + 6, y + 2),
                          ImVec2(x, y - 6), col);
  };

  auto draw_diamond = [&](float t, ImU32 col) {
    float x = X(t);
    float y = (p0.y + p1.y) * 0.5f;

    dl->AddQuadFilled(ImVec2(x, y - 6), ImVec2(x + 6, y), ImVec2(x, y + 6),
                      ImVec2(x - 6, y), col);

    // guide line
    dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), IM_COL32(255, 255, 255, 80));
  };

  draw_tri_up(t_low, IM_COL32(255, 255, 255, 255));
  draw_tri_up(t_high, IM_COL32(255, 255, 255, 255));
  draw_diamond(t_gamma, IM_COL32(220, 220, 220, 255));

  ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y + 14));
  ImVec2 p2 = ImGui::GetCursorScreenPos();
  ImVec2 p3 = ImVec2(p2.x + width, p2.y + height);

  dl->AddRectFilledMultiColor(
      p2, p3, IM_COL32(0, 0, 0, 255), IM_COL32(255, 255, 255, 255),
      IM_COL32(255, 255, 255, 255), IM_COL32(0, 0, 0, 255));

  ImGui::InvisibleButton("output_bar", ImVec2(width, height + 12));
  active = ImGui::IsItemActive();

  mouse_t = (ImGui::GetIO().MousePos.x - p2.x) / width;
  mouse_t = std::clamp(mouse_t, 0.0f, 1.0f);

  float d_ol = fabsf(mouse_t - (float)out_low);
  float d_oh = fabsf(mouse_t - (float)out_high);

  if (active && ImGui::IsMouseDragging(0)) {
    if (d_ol < d_oh)
      out_low = std::min((double)mouse_t, out_high - 0.001);
    else
      out_high = std::max((double)mouse_t, out_low + 0.001);
    changed = true;
  }

  auto draw_tri_down = [&](float t, ImU32 col) {
    float x = p2.x + t * width;
    float y = p2.y;
    dl->AddTriangleFilled(ImVec2(x - 6, y - 2), ImVec2(x + 6, y - 2),
                          ImVec2(x, y + 6), col);
  };

  draw_tri_down((float)out_low, IM_COL32(255, 255, 255, 255));
  draw_tri_down((float)out_high, IM_COL32(255, 255, 255, 255));
  ImGui::PopID();

  return changed;
}

void ImageEditor::render_controls() {
  ImGui::SeparatorText("Tone & Exposure");
  if (gegl_has_operation("gegl:exposure") && ImGui::TreeNode("Exposure")) {
    const EffectType type = EffectType::Exposure;
    bool active = is_effect_active(type);
    if (ImGui::Checkbox("Enabled##Exp", &active)) {
      if (active)
        get_or_create_effect(type);
      else
        remove_effect(type);
      apply_gegl_texture();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##Exp")) {
      exposure_state = ExposureState();
      if (active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "black-level",
                      (gdouble)exposure_state.black_level, "exposure",
                      (gdouble)exposure_state.exposure, NULL);
        apply_gegl_texture();
      }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted(
          "Exposure adjustment in the linear light domain — applies a "
          "black-level offset and an exposure value in stops, matching what "
          "you would adjust on a camera before a shot.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    bool changed = false;
    static const double black_min = -0.1, black_max = 0.1;
    static const double exp_min = -10.0, exp_max = 10.0;
    changed |= ImGui::SliderScalar("Black Level", ImGuiDataType_Double,
                                   &exposure_state.black_level, &black_min,
                                   &black_max, "%.3f");
    changed |= ImGui::SliderScalar("Exposure (EV)", ImGuiDataType_Double,
                                   &exposure_state.exposure, &exp_min, &exp_max,
                                   "%.2f");

    if (changed && active) {
      Effect &e = get_or_create_effect(type);
      gegl_node_set(e.node, "black-level", (gdouble)exposure_state.black_level,
                    "exposure", (gdouble)exposure_state.exposure, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (gegl_has_operation("gegl:shadows-highlights") &&
      ImGui::TreeNode("Shadows & Highlights")) {
    const EffectType type = EffectType::ShadowsHighlights;
    bool active = is_effect_active(type);
    if (ImGui::Checkbox("Enabled##SH", &active)) {
      if (active)
        get_or_create_effect(type);
      else
        remove_effect(type);
      apply_gegl_texture();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##SH")) {
      shadows_highlights_state = ShadowsHighlightsState();
      if (active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(
            e.node, "shadows", (gdouble)shadows_highlights_state.shadows,
            "highlights", (gdouble)shadows_highlights_state.highlights,
            "whitepoint", (gdouble)shadows_highlights_state.whitepoint,
            "radius", (gdouble)shadows_highlights_state.radius, "compress",
            (gdouble)shadows_highlights_state.compress, "shadows-ccorrect",
            (gdouble)shadows_highlights_state.shadows_ccorrect,
            "highlights-ccorrect",
            (gdouble)shadows_highlights_state.highlights_ccorrect, NULL);
        apply_gegl_texture();
      }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted(
          "Independently brightens shadow regions and darkens highlight "
          "regions of the image, compressing the overall tonal range. A "
          "spatial radius controls how locally the effect is computed.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    bool changed = false;
    static const double sh_min = -100.0, sh_max = 100.0;
    static const double wp_min = -10.0, wp_max = 10.0;
    static const double r_min = 0.1, r_max = 1500.0;
    static const double c_min = 0.0, c_max = 100.0;
    changed |= ImGui::SliderScalar("Shadows", ImGuiDataType_Double,
                                   &shadows_highlights_state.shadows, &sh_min,
                                   &sh_max, "%.1f");
    changed |= ImGui::SliderScalar("Highlights", ImGuiDataType_Double,
                                   &shadows_highlights_state.highlights,
                                   &sh_min, &sh_max, "%.1f");
    changed |= ImGui::SliderScalar("White Point", ImGuiDataType_Double,
                                   &shadows_highlights_state.whitepoint,
                                   &wp_min, &wp_max, "%.2f");
    changed |= ImGui::SliderScalar("Radius", ImGuiDataType_Double,
                                   &shadows_highlights_state.radius, &r_min,
                                   &r_max, "%.1f");
    changed |= ImGui::SliderScalar("Compress", ImGuiDataType_Double,
                                   &shadows_highlights_state.compress, &c_min,
                                   &c_max, "%.1f");
    changed |= ImGui::SliderScalar(
        "Shadows Color Adjustment", ImGuiDataType_Double,
        &shadows_highlights_state.shadows_ccorrect, &c_min, &c_max, "%.1f");
    changed |= ImGui::SliderScalar(
        "Highlights Color Adjustment", ImGuiDataType_Double,
        &shadows_highlights_state.highlights_ccorrect, &c_min, &c_max, "%.1f");

    if (changed && active) {
      Effect &e = get_or_create_effect(type);
      gegl_node_set(
          e.node, "shadows", (gdouble)shadows_highlights_state.shadows,
          "highlights", (gdouble)shadows_highlights_state.highlights,
          "whitepoint", (gdouble)shadows_highlights_state.whitepoint, "radius",
          (gdouble)shadows_highlights_state.radius, "compress",
          (gdouble)shadows_highlights_state.compress, "shadows-ccorrect",
          (gdouble)shadows_highlights_state.shadows_ccorrect,
          "highlights-ccorrect",
          (gdouble)shadows_highlights_state.highlights_ccorrect, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Levels")) {
    const EffectType type = EffectType::Levels;
    bool active = is_effect_active(type);

    if (ImGui::Checkbox("Enabled##Lv", &active)) {
      if (active)
        get_or_create_effect(type);
      else
        remove_effect(type);
      apply_gegl_texture();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##Lv")) {
      levels_state = LevelsState();
      if (active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "in-low", (gdouble)levels_state.in_low, "in-high",
                      (gdouble)levels_state.in_high, "gamma",
                      (gdouble)levels_state.gamma, "out-low",
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
        apply_gegl_texture();
      }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted(
          "Per-channel input/output levels with gamma midtone adjustment.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    bool changed = false;

    ImGui::SeparatorText("Composite");
    changed |= DrawLevelsBar("C", levels_state.in_low, levels_state.gamma,
                             levels_state.in_high, levels_state.out_low,
                             levels_state.out_high, ImVec4(0, 0, 0, 1),
                             ImVec4(1, 1, 1, 1));

    ImGui::SeparatorText("Red Channel");
    changed |= DrawLevelsBar("R", levels_state.in_low_r, levels_state.gamma_r,
                             levels_state.in_high_r, levels_state.out_low_r,
                             levels_state.out_high_r, ImVec4(0, 0, 0, 1),
                             ImVec4(1, 0, 0, 1));

    ImGui::SeparatorText("Green Channel");
    changed |= DrawLevelsBar("G", levels_state.in_low_g, levels_state.gamma_g,
                             levels_state.in_high_g, levels_state.out_low_g,
                             levels_state.out_high_g, ImVec4(0, 0, 0, 1),
                             ImVec4(0, 1, 0, 1));

    ImGui::SeparatorText("Blue Channel");
    changed |= DrawLevelsBar("B", levels_state.in_low_b, levels_state.gamma_b,
                             levels_state.in_high_b, levels_state.out_low_b,
                             levels_state.out_high_b, ImVec4(0, 0, 0, 1),
                             ImVec4(0, 0, 1, 1));

    if (changed && active) {
      Effect &e = get_or_create_effect(type);
      gegl_node_set(e.node, "in-low", (gdouble)levels_state.in_low, "in-high",
                    (gdouble)levels_state.in_high, "gamma",
                    (gdouble)levels_state.gamma, "out-low",
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
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  ImGui::SeparatorText("Colour");
  if (gegl_has_operation("gegl:color-temperature") &&
      ImGui::TreeNode("Color Temperature")) {
    const EffectType type = EffectType::ColorTemperature;
    bool active = is_effect_active(type);
    if (ImGui::Checkbox("Enabled##CT", &active)) {
      if (active)
        get_or_create_effect(type);
      else
        remove_effect(type);
      apply_gegl_texture();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##CT")) {
      color_temperature_state = ColorTemperatureState();
      if (active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "original-temperature",
                      (gdouble)color_temperature_state.original_temperature,
                      "intended-temperature",
                      (gdouble)color_temperature_state.intended_temperature,
                      NULL);
        apply_gegl_texture();
      }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted(
          "Changes the colour temperature of the image, from an assumed "
          "original colour temperature to an intended one. Both values are in "
          "Kelvin — lower is warmer (orange), higher is cooler (blue).");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    bool changed = false;
    static const double t_min = 1000.0, t_max = 12000.0;
    changed |=
        ImGui::SliderScalar("Original", ImGuiDataType_Double,
                            &color_temperature_state.original_temperature,
                            &t_min, &t_max, "%.0f K");
    changed |=
        ImGui::SliderScalar("Intended", ImGuiDataType_Double,
                            &color_temperature_state.intended_temperature,
                            &t_min, &t_max, "%.0f K");

    if (changed && active) {
      Effect &e = get_or_create_effect(type);
      gegl_node_set(e.node, "original-temperature",
                    (gdouble)color_temperature_state.original_temperature,
                    "intended-temperature",
                    (gdouble)color_temperature_state.intended_temperature,
                    NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (gegl_has_operation("gegl:hue-chroma") && ImGui::TreeNode("Hue-Chroma")) {
    const EffectType type = EffectType::HueChroma;
    bool active = is_effect_active(type);
    if (ImGui::Checkbox("Enabled##HC", &active)) {
      if (active)
        get_or_create_effect(type);
      else
        remove_effect(type);
      apply_gegl_texture();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##HC")) {
      hue_chroma_state = HueChromaState();
      if (active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "hue", (gdouble)hue_chroma_state.hue, "chroma",
                      (gdouble)hue_chroma_state.chroma, "lightness",
                      (gdouble)hue_chroma_state.lightness, NULL);
        apply_gegl_texture();
      }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted(
          "Adjusts hue, chroma (saturation), and lightness in a perceptually "
          "uniform colour space. Produces cleaner results than naive HSL "
          "adjustment, especially when pushing chroma.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    bool changed = false;
    static const double hue_min = -180.0, hue_max = 180.0;
    static const double chroma_min = -100.0, chroma_max = 100.0;
    static const double lightness_min = -100.0, lightness_max = 100.0;
    changed |=
        ImGui::SliderScalar("Hue", ImGuiDataType_Double, &hue_chroma_state.hue,
                            &hue_min, &hue_max, "%.1f");
    changed |= ImGui::SliderScalar("Chroma", ImGuiDataType_Double,
                                   &hue_chroma_state.chroma, &chroma_min,
                                   &chroma_max, "%.1f");
    changed |= ImGui::SliderScalar("Lightness", ImGuiDataType_Double,
                                   &hue_chroma_state.lightness, &lightness_min,
                                   &lightness_max, "%.1f");

    if (changed && active) {
      Effect &e = get_or_create_effect(type);
      gegl_node_set(e.node, "hue", (gdouble)hue_chroma_state.hue, "chroma",
                    (gdouble)hue_chroma_state.chroma, "lightness",
                    (gdouble)hue_chroma_state.lightness, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (gegl_has_operation("gegl:color-enhance") &&
      ImGui::TreeNode("Color Enhance")) {
    const EffectType type = EffectType::ColorEnhance;
    bool active = is_effect_active(type);
    if (ImGui::Checkbox("Enabled##CE", &active)) {
      if (active) {
        get_or_create_effect(type);
        color_enhance_state.enabled = true;
      } else {
        remove_effect(type);
        color_enhance_state.enabled = false;
      }
      apply_gegl_texture();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted(
          "Stretches the colour chroma to cover the maximum possible range, "
          "keeping hue and lightness untouched. Useful for images that look "
          "dull without being over or under exposed.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }
    ImGui::TreePop();
  }

  if (gegl_has_operation("gegl:saturation") && ImGui::TreeNode("Saturation")) {
    const EffectType type = EffectType::Saturation;
    bool active = is_effect_active(type);
    if (ImGui::Checkbox("Enabled##Sat", &active)) {
      if (active)
        get_or_create_effect(type);
      else
        remove_effect(type);
      apply_gegl_texture();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##Sat")) {
      saturation_state = SaturationState();
      if (active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "scale", (gdouble)saturation_state.scale, NULL);
        apply_gegl_texture();
      }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted(
          "Changes the saturation of the image. Scale is a multiplier on the "
          "existing saturation — 1.0 is unchanged, 0.0 is fully desaturated, "
          "2.0 doubles saturation.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    bool changed = false;
    static const double s_min = 0.0, s_max = 2.0;
    changed |=
        ImGui::SliderScalar("Scale", ImGuiDataType_Double,
                            &saturation_state.scale, &s_min, &s_max, "%.2f");

    if (changed && active) {
      Effect &e = get_or_create_effect(type);
      gegl_node_set(e.node, "scale", (gdouble)saturation_state.scale, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (gegl_has_operation("gegl:sepia") && ImGui::TreeNode("Sepia")) {
    const EffectType type = EffectType::Sepia;
    bool active = is_effect_active(type);
    if (ImGui::Checkbox("Enabled##Sepia", &active)) {
      if (active)
        get_or_create_effect(type);
      else
        remove_effect(type);
      apply_gegl_texture();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##Sepia")) {
      sepia_state = SepiaState();
      if (active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "scale", (gdouble)sepia_state.scale, NULL);
        apply_gegl_texture();
      }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted("Apply a sepia tone to the input image.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    bool changed = false;
    static const double s_min = 0.0, s_max = 1.0;
    changed |= ImGui::SliderScalar("Effect Strength", ImGuiDataType_Double,
                                   &sepia_state.scale, &s_min, &s_max, "%.2f");

    if (changed && active) {
      Effect &e = get_or_create_effect(type);
      gegl_node_set(e.node, "scale", (gdouble)sepia_state.scale, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (gegl_has_operation("gegl:mono-mixer") && ImGui::TreeNode("Mono Mixer")) {
    const EffectType type = EffectType::MonoMixer;
    bool active = is_effect_active(type);
    if (ImGui::Checkbox("Enabled##MM", &active)) {
      if (active)
        get_or_create_effect(type);
      else
        remove_effect(type);
      apply_gegl_texture();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##MM")) {
      mono_mixer_state = MonoMixerState();
      if (active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "red", (gdouble)mono_mixer_state.red, "green",
                      (gdouble)mono_mixer_state.green, "blue",
                      (gdouble)mono_mixer_state.blue, "preserve-luminosity",
                      (gboolean)mono_mixer_state.preserve_luminosity, NULL);
        apply_gegl_texture();
      }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted("Monochrome channel mixer.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    bool changed = false;
    static const double mm_min = -2.0, mm_max = 2.0;
    changed |=
        ImGui::SliderScalar("Red", ImGuiDataType_Double, &mono_mixer_state.red,
                            &mm_min, &mm_max, "%.3f");
    changed |=
        ImGui::SliderScalar("Green", ImGuiDataType_Double,
                            &mono_mixer_state.green, &mm_min, &mm_max, "%.3f");
    changed |=
        ImGui::SliderScalar("Blue", ImGuiDataType_Double,
                            &mono_mixer_state.blue, &mm_min, &mm_max, "%.3f");
    changed |= ImGui::Checkbox("Preserve Luminosity",
                               &mono_mixer_state.preserve_luminosity);

    if (changed && active) {
      Effect &e = get_or_create_effect(type);
      gegl_node_set(e.node, "red", (gdouble)mono_mixer_state.red, "green",
                    (gdouble)mono_mixer_state.green, "blue",
                    (gdouble)mono_mixer_state.blue, "preserve-luminosity",
                    (gboolean)mono_mixer_state.preserve_luminosity, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (gegl_has_operation("gegl:stretch-contrast") &&
      ImGui::TreeNode("Stretch Contrast")) {
    const EffectType type = EffectType::StretchContrast;
    bool active = is_effect_active(type);
    if (ImGui::Checkbox("Enabled##SC", &active)) {
      if (active) {
        get_or_create_effect(type);
        stretch_contrast_state.enabled = true;
      } else {
        remove_effect(type);
        stretch_contrast_state.enabled = false;
      }
      apply_gegl_texture();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted(
          "Scales the components of the buffer to be in the 0.0–1.0 range. "
          "Improves images that make poor use of the available contrast — "
          "little contrast, very dark, or very bright images.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }
    ImGui::TreePop();
  }

  if (gegl_has_operation("gegl:stretch-contrast-hsv") &&
      ImGui::TreeNode("Stretch Contrast HSV")) {
    const EffectType type = EffectType::StretchContrastHSV;
    bool active = is_effect_active(type);
    if (ImGui::Checkbox("Enabled##SCH", &active)) {
      if (active) {
        get_or_create_effect(type);
        stretch_contrast_hsv_state.enabled = true;
      } else {
        remove_effect(type);
        stretch_contrast_hsv_state.enabled = false;
      }
      apply_gegl_texture();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted(
          "Like Stretch Contrast but works in HSV colour space, preserving "
          "hue. Generally looks more natural on colour photographs.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }
    ImGui::TreePop();
  }

  ImGui::SeparatorText("Sharpening");
  if (gegl_has_operation("gegl:unsharp-mask") &&
      ImGui::TreeNode("Unsharp Mask")) {
    const EffectType type = EffectType::UnsharpMask;
    bool active = is_effect_active(type);
    if (ImGui::Checkbox("Enabled##UM", &active)) {
      if (active)
        get_or_create_effect(type);
      else
        remove_effect(type);
      apply_gegl_texture();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##UM")) {
      unsharp_mask_state = UnsharpMaskState();
      if (active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "std-dev", (gdouble)unsharp_mask_state.std_dev,
                      "scale", (gdouble)unsharp_mask_state.scale, "threshold",
                      (gdouble)unsharp_mask_state.threshold, NULL);
        apply_gegl_texture();
      }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted(
          "Sharpens the image by adding the difference between the original "
          "and a blurred version back on top — a technique originally used in "
          "darkrooms. Std. Dev controls the radius of the blur used to compute "
          "the mask.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    bool changed = false;
    static const double r_min = 0.0, r_max = 120.0;
    static const double a_min = 0.0, a_max = 10.0;
    static const double t_min = 0.0, t_max = 1.0;
    changed |= ImGui::SliderScalar("Radius", ImGuiDataType_Double,
                                   &unsharp_mask_state.std_dev, &r_min, &r_max,
                                   "%.1f");
    changed |=
        ImGui::SliderScalar("Amount", ImGuiDataType_Double,
                            &unsharp_mask_state.scale, &a_min, &a_max, "%.2f");
    changed |= ImGui::SliderScalar("Threshold", ImGuiDataType_Double,
                                   &unsharp_mask_state.threshold, &t_min,
                                   &t_max, "%.3f");

    if (changed && active) {
      Effect &e = get_or_create_effect(type);
      gegl_node_set(e.node, "std-dev", (gdouble)unsharp_mask_state.std_dev,
                    "scale", (gdouble)unsharp_mask_state.scale, "threshold",
                    (gdouble)unsharp_mask_state.threshold, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  ImGui::SeparatorText("Noise Reduction");
  if (gegl_has_operation("gegl:noise-reduction") &&
      ImGui::TreeNode("Noise Reduction")) {
    const EffectType type = EffectType::NoiseReduction;
    bool active = is_effect_active(type);
    if (ImGui::Checkbox("Enabled##NR", &active)) {
      if (active)
        get_or_create_effect(type);
      else
        remove_effect(type);
      apply_gegl_texture();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##NR")) {
      noise_reduction_state = NoiseReductionState();
      if (active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "iterations",
                      (gint)noise_reduction_state.iterations, NULL);
        apply_gegl_texture();
      }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted(
          "Anisotropic smoothing operation reduces noise while attempting to "
          "preserve edges. Iterations controls the strength; more passes give "
          "a smoother result.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    bool changed = false;
    static const int i_min = 0, i_max = 8;
    changed |= ImGui::SliderInt("Iterations", &noise_reduction_state.iterations,
                                i_min, i_max);

    if (changed && active) {
      Effect &e = get_or_create_effect(type);
      gegl_node_set(e.node, "iterations",
                    (gint)noise_reduction_state.iterations, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (gegl_has_operation("gegl:snn-mean") && ImGui::TreeNode("SNN Mean")) {
    const EffectType type = EffectType::SNNMean;
    bool active = is_effect_active(type);
    if (ImGui::Checkbox("Enabled##SNN", &active)) {
      if (active)
        get_or_create_effect(type);
      else
        remove_effect(type);
      apply_gegl_texture();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##SNN")) {
      snn_mean_state = SNNMeanState();
      if (active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "radius", (gint)snn_mean_state.radius, "pairs",
                      (gint)snn_mean_state.pairs, NULL);
        apply_gegl_texture();
      }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted(
          "Noise-reducing edge-preserving blur based on Symmetric Nearest "
          "Neighbours. For each pixel, picks the most similar neighbours from "
          "symmetric pairs to average, avoiding smearing across edges.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    bool changed = false;
    static const int r_min = 1, r_max = 60;
    static const int p_min = 1, p_max = 2;
    changed |= ImGui::SliderInt("Radius", &snn_mean_state.radius, r_min, r_max);
    changed |= ImGui::SliderInt("Pairs", &snn_mean_state.pairs, p_min, p_max);

    if (changed && active) {
      Effect &e = get_or_create_effect(type);
      gegl_node_set(e.node, "radius", (gint)snn_mean_state.radius, "pairs",
                    (gint)snn_mean_state.pairs, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }
}
