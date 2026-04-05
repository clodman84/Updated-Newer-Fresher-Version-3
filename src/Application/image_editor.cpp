#include "image_editor.h"
#include "gegl-node.h"
#include "gegl-types.h"
#include "gpu_utils.h"
#include "imgui.h"
#include "stb_image.h"
#include <gegl.h>

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
  if (sink == nullptr)
    return;

  GeglRectangle roi = {0, 0, width, height};
  const size_t buf_size = static_cast<size_t>(width) * height * 4;
  unsigned char *pixels = static_cast<unsigned char *>(IM_ALLOC(buf_size));
  if (pixels == nullptr)
    return;

  gegl_node_blit(sink,
                 1.0, // scale — 1:1 since you already downsampled via stbir
                 &roi, babl_format("R'G'B'A u8"), pixels, GEGL_AUTO_ROWSTRIDE,
                 GEGL_BLIT_DEFAULT);

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
  case EffectType::BrightnessContrast:
    e.node = gegl_node_new_child(
        graph, "operation", "gegl:brightness-contrast", "brightness",
        (gdouble)brightness_contrast_state.brightness, "contrast",
        (gdouble)brightness_contrast_state.contrast, NULL);
    break;
  case EffectType::Exposure:
    e.node = gegl_node_new_child(
        graph, "operation", "gegl:exposure", "black-level",
        (gdouble)exposure_state.black_level, "exposure",
        (gdouble)exposure_state.exposure, NULL);
    break;
  case EffectType::ShadowsHighlights:
    e.node = gegl_node_new_child(
        graph, "operation", "gegl:shadows-highlights", "shadows",
        (gdouble)shadows_highlights_state.shadows, "highlights",
        (gdouble)shadows_highlights_state.highlights, "whitepoint",
        (gdouble)shadows_highlights_state.whitepoint, "radius",
        (gdouble)shadows_highlights_state.radius, NULL);
    break;
  case EffectType::Levels:
    e.node = gegl_node_new_child(
        graph, "operation", "gegl:levels", "in-low",
        (gdouble)levels_state.in_low, "in-high", (gdouble)levels_state.in_high,
        "out-low", (gdouble)levels_state.out_low, "out-high",
        (gdouble)levels_state.out_high, NULL);
    break;
  case EffectType::ColorTemperature:
    e.node = gegl_node_new_child(
        graph, "operation", "gegl:color-temperature", "original-temperature",
        (gdouble)color_temperature_state.original_temperature,
        "intended-temperature",
        (gdouble)color_temperature_state.intended_temperature, NULL);
    break;
  case EffectType::HueChroma:
    e.node = gegl_node_new_child(
        graph, "operation", "gegl:hue-chroma", "hue",
        (gdouble)hue_chroma_state.hue, "chroma", (gdouble)hue_chroma_state.chroma,
        "lightness", (gdouble)hue_chroma_state.lightness, NULL);
    break;
  case EffectType::Saturation:
    e.node = gegl_node_new_child(graph, "operation", "gegl:saturation", "scale",
                                 (gdouble)saturation_state.scale, NULL);
    break;
  case EffectType::ColorEnhance:
    e.node = gegl_node_new_child(graph, "operation", "gegl:color-enhance", NULL);
    break;
  case EffectType::StretchContrast:
    e.node =
        gegl_node_new_child(graph, "operation", "gegl:stretch-contrast", NULL);
    break;
  case EffectType::StretchContrastHSV:
    e.node = gegl_node_new_child(graph, "operation", "gegl:stretch-contrast-hsv",
                                 NULL);
    break;
  case EffectType::UnsharpMask:
    e.node = gegl_node_new_child(
        graph, "operation", "gegl:unsharp-mask", "std-dev",
        (gdouble)unsharp_mask_state.std_dev, "scale",
        (gdouble)unsharp_mask_state.scale, "threshold",
        (gdouble)unsharp_mask_state.threshold, NULL);
    break;
  case EffectType::HighPass:
    e.node = gegl_node_new_child(
        graph, "operation", "gegl:high-pass", "std-dev",
        (gdouble)high_pass_state.std_dev, "contrast",
        (gdouble)high_pass_state.contrast, NULL);
    break;
  case EffectType::NoiseReduction:
    e.node = gegl_node_new_child(graph, "operation", "gegl:noise-reduction",
                                 "iterations",
                                 (gint)noise_reduction_state.iterations, NULL);
    break;
  case EffectType::SNNMean:
    e.node = gegl_node_new_child(
        graph, "operation", "gegl:snn-mean", "radius",
        (gint)snn_mean_state.radius, "pairs", (gint)snn_mean_state.pairs, NULL);
    break;
  case EffectType::DomainTransform:
    e.node = gegl_node_new_child(
        graph, "operation", "gegl:domain-transform", "sigma-s",
        (gdouble)domain_transform_state.sigma_s, "sigma-r",
        (gdouble)domain_transform_state.sigma_r, "n-iterations",
        (gint)domain_transform_state.n_iterations, NULL);
    break;
  case EffectType::BilateralFilter:
    e.node = gegl_node_new_child(
        graph, "operation", "gegl:bilateral-filter", "blur-radius",
        (gdouble)bilateral_filter_state.blur_radius, "edge-preservation",
        (gdouble)bilateral_filter_state.edge_preservation, NULL);
    break;
  case EffectType::LocalContrast:
    e.node = gegl_node_new_child(
        graph, "operation", "gegl:local-contrast", "radius",
        (gdouble)local_contrast_state.radius, "amount",
        (gdouble)local_contrast_state.amount, NULL);
    break;
  }

  GeglNode *before_sink = effects.empty() ? source : effects.back().node;

  gegl_node_link(before_sink, e.node);
  gegl_node_link(e.node, sink);

  effects.emplace_back(e);
  return effects.back();
}

void ImageEditor::render_controls() {
  ImGui::SeparatorText("Tone & Exposure");
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
      gegl_node_set(e.node, "brightness", (gdouble)brightness_contrast_state.brightness,
                    "contrast", (gdouble)brightness_contrast_state.contrast, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Exposure")) {
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
                                   &exposure_state.exposure, &exp_min,
                                   &exp_max, "%.2f");

    if (changed) {
      Effect &e = get_or_create_effect(EffectType::Exposure);
      gegl_node_set(e.node, "black-level", (gdouble)exposure_state.black_level,
                    "exposure", (gdouble)exposure_state.exposure, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Shadows & Highlights")) {
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

    if (changed) {
      Effect &e = get_or_create_effect(EffectType::ShadowsHighlights);
      gegl_node_set(e.node, "shadows", (gdouble)shadows_highlights_state.shadows,
                    "highlights", (gdouble)shadows_highlights_state.highlights,
                    "whitepoint", (gdouble)shadows_highlights_state.whitepoint,
                    "radius", (gdouble)shadows_highlights_state.radius, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Levels")) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted(
          "Set input and output black and white points to remap the tonal "
          "range. Drag the input points inward to increase contrast; drag "
          "output points inward to limit the range.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    bool changed = false;
    static const double l_min = 0.0, l_max = 1.0;
    changed |= ImGui::SliderScalar("Input Low", ImGuiDataType_Double,
                                   &levels_state.in_low, &l_min, &l_max, "%.3f");
    changed |= ImGui::SliderScalar("Input High", ImGuiDataType_Double,
                                   &levels_state.in_high, &l_min, &l_max, "%.3f");
    changed |= ImGui::SliderScalar("Output Low", ImGuiDataType_Double,
                                   &levels_state.out_low, &l_min, &l_max, "%.3f");
    changed |= ImGui::SliderScalar("Output High", ImGuiDataType_Double,
                                   &levels_state.out_high, &l_min, &l_max,
                                   "%.3f");

    if (changed) {
      Effect &e = get_or_create_effect(EffectType::Levels);
      gegl_node_set(e.node, "in-low", (gdouble)levels_state.in_low, "in-high",
                    (gdouble)levels_state.in_high, "out-low",
                    (gdouble)levels_state.out_low, "out-high",
                    (gdouble)levels_state.out_high, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  ImGui::SeparatorText("Colour");
  if (ImGui::TreeNode("Color Temperature")) {
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
    changed |= ImGui::SliderScalar("Original", ImGuiDataType_Double,
                                   &color_temperature_state.original_temperature,
                                   &t_min, &t_max, "%.0f K");
    changed |= ImGui::SliderScalar("Intended", ImGuiDataType_Double,
                                   &color_temperature_state.intended_temperature,
                                   &t_min, &t_max, "%.0f K");

    if (changed) {
      Effect &e = get_or_create_effect(EffectType::ColorTemperature);
      gegl_node_set(e.node, "original-temperature",
                    (gdouble)color_temperature_state.original_temperature,
                    "intended-temperature",
                    (gdouble)color_temperature_state.intended_temperature, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Hue-Chroma")) {
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
    changed |= ImGui::SliderScalar("Hue", ImGuiDataType_Double,
                                   &hue_chroma_state.hue, &hue_min, &hue_max,
                                   "%.1f");
    changed |= ImGui::SliderScalar("Chroma", ImGuiDataType_Double,
                                   &hue_chroma_state.chroma, &chroma_min,
                                   &chroma_max, "%.1f");
    changed |= ImGui::SliderScalar("Lightness", ImGuiDataType_Double,
                                   &hue_chroma_state.lightness, &lightness_min,
                                   &lightness_max, "%.1f");

    if (changed) {
      Effect &e = get_or_create_effect(EffectType::HueChroma);
      gegl_node_set(e.node, "hue", (gdouble)hue_chroma_state.hue, "chroma",
                    (gdouble)hue_chroma_state.chroma, "lightness",
                    (gdouble)hue_chroma_state.lightness, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Saturation")) {
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
    changed |= ImGui::SliderScalar("Scale", ImGuiDataType_Double,
                                   &saturation_state.scale, &s_min, &s_max,
                                   "%.2f");

    if (changed) {
      Effect &e = get_or_create_effect(EffectType::Saturation);
      gegl_node_set(e.node, "scale", (gdouble)saturation_state.scale, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Color Enhance")) {
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

    if (ImGui::Checkbox("Enable", &color_enhance_state.enabled)) {
      // TODO: support effect removal
      if (color_enhance_state.enabled) {
        get_or_create_effect(EffectType::ColorEnhance);
      }
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Stretch Contrast")) {
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

    if (ImGui::Checkbox("Enable", &stretch_contrast_state.enabled)) {
      // TODO: support effect removal
      if (stretch_contrast_state.enabled) {
        get_or_create_effect(EffectType::StretchContrast);
      }
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Stretch Contrast HSV")) {
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

    if (ImGui::Checkbox("Enable", &stretch_contrast_hsv_state.enabled)) {
      // TODO: support effect removal
      if (stretch_contrast_hsv_state.enabled) {
        get_or_create_effect(EffectType::StretchContrastHSV);
      }
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  ImGui::SeparatorText("Sharpening");
  if (ImGui::TreeNode("Unsharp Mask")) {
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
    changed |= ImGui::SliderScalar("Amount", ImGuiDataType_Double,
                                   &unsharp_mask_state.scale, &a_min, &a_max,
                                   "%.2f");
    changed |= ImGui::SliderScalar("Threshold", ImGuiDataType_Double,
                                   &unsharp_mask_state.threshold, &t_min,
                                   &t_max, "%.3f");

    if (changed) {
      Effect &e = get_or_create_effect(EffectType::UnsharpMask);
      gegl_node_set(e.node, "std-dev", (gdouble)unsharp_mask_state.std_dev,
                    "scale", (gdouble)unsharp_mask_state.scale, "threshold",
                    (gdouble)unsharp_mask_state.threshold, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("High Pass")) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted(
          "Enhances fine details by isolating high-frequency content. The "
          "result is a grey midtone image where only edges and texture "
          "survive. Best used for clarity-style midtone contrast enhancement.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    bool changed = false;
    static const double r_min = 0.0, r_max = 1000.0;
    static const double c_min = 0.0, c_max = 5.0;
    changed |= ImGui::SliderScalar("Radius", ImGuiDataType_Double,
                                   &high_pass_state.std_dev, &r_min, &r_max,
                                   "%.1f");
    changed |= ImGui::SliderScalar("Contrast", ImGuiDataType_Double,
                                   &high_pass_state.contrast, &c_min, &c_max,
                                   "%.2f");

    if (changed) {
      Effect &e = get_or_create_effect(EffectType::HighPass);
      gegl_node_set(e.node, "std-dev", (gdouble)high_pass_state.std_dev,
                    "contrast", (gdouble)high_pass_state.contrast, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  ImGui::SeparatorText("Noise Reduction");
  if (ImGui::TreeNode("Noise Reduction")) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted(
          "Anisotropic smoothing operation — reduces noise while attempting to "
          "preserve edges. Iterations controls the strength; more passes give "
          "a smoother result.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    bool changed = false;
    static const int i_min = 0, i_max = 8;
    changed |= ImGui::SliderInt("Iterations", &noise_reduction_state.iterations,
                                i_min, i_max);

    if (changed) {
      Effect &e = get_or_create_effect(EffectType::NoiseReduction);
      gegl_node_set(e.node, "iterations", (gint)noise_reduction_state.iterations,
                    NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("SNN Mean")) {
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

    if (changed) {
      Effect &e = get_or_create_effect(EffectType::SNNMean);
      gegl_node_set(e.node, "radius", (gint)snn_mean_state.radius, "pairs",
                    (gint)snn_mean_state.pairs, NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Domain Transform")) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted(
          "An edge-preserving smoothing filter implemented with the Domain "
          "Transform recursive technique. Similar to a bilateral filter but "
          "significantly faster to compute.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    bool changed = false;
    static const double ss_min = 0.0, ss_max = 300.0;
    static const double sr_min = 0.0, sr_max = 1.0;
    static const int ni_min = 1, ni_max = 10;
    changed |= ImGui::SliderScalar("Sigma S", ImGuiDataType_Double,
                                   &domain_transform_state.sigma_s, &ss_min,
                                   &ss_max, "%.1f");
    changed |= ImGui::SliderScalar("Sigma R", ImGuiDataType_Double,
                                   &domain_transform_state.sigma_r, &sr_min,
                                   &sr_max, "%.3f");
    changed |= ImGui::SliderInt("Iterations",
                                &domain_transform_state.n_iterations, ni_min,
                                ni_max);

    if (changed) {
      Effect &e = get_or_create_effect(EffectType::DomainTransform);
      gegl_node_set(e.node, "sigma-s", (gdouble)domain_transform_state.sigma_s,
                    "sigma-r", (gdouble)domain_transform_state.sigma_r,
                    "n-iterations", (gint)domain_transform_state.n_iterations,
                    NULL);
      apply_gegl_texture();
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Bilateral Filter")) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted(
          "Like a Gaussian blur, but each neighbouring pixel's contribution is "
          "also weighted by the colour difference with the original centre "
          "pixel. Preserves edges while blurring flat areas.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    bool changed = false;
    static const double br_min = 1.0, br_max = 40.0;
    static const double ep_min = 0.0, ep_max = 1.0;
    changed |= ImGui::SliderScalar("Blur Radius", ImGuiDataType_Double,
                                   &bilateral_filter_state.blur_radius, &br_min,
                                   &br_max, "%.1f");
    changed |= ImGui::SliderScalar(
        "Edge Preservation", ImGuiDataType_Double,
        &bilateral_filter_state.edge_preservation, &ep_min, &ep_max, "%.3f");

    bilateral_filter_state.dirty |= changed;
    ImGui::Spacing();
    ImGui::TextDisabled("! May be slow on large images.");
    ImGui::BeginDisabled(!bilateral_filter_state.dirty);
    if (ImGui::Button("Apply##Bilateral")) {
      Effect &e = get_or_create_effect(EffectType::BilateralFilter);
      gegl_node_set(e.node, "blur-radius",
                    (gdouble)bilateral_filter_state.blur_radius,
                    "edge-preservation",
                    (gdouble)bilateral_filter_state.edge_preservation, NULL);
      apply_gegl_texture();
      bilateral_filter_state.dirty = false;
    }
    ImGui::EndDisabled();
    ImGui::TreePop();
  }

  ImGui::SeparatorText("Local Contrast");
  if (ImGui::TreeNode("Local Contrast")) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(300.0f);
      ImGui::TextUnformatted(
          "Increases contrast in local neighbourhoods across the image, "
          "bringing out midtone detail without clipping highlights or shadows "
          "globally.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    unsigned int n_properties;
    GParamSpec **properties =
        gegl_operation_list_properties("gegl:local-contrast", &n_properties);
    if (properties && n_properties > 0) {
      bool changed = false;
      static const double r_min = 0.1, r_max = 1000.0;
      static const double a_min = 0.0, a_max = 10.0;
      changed |= ImGui::SliderScalar("Radius", ImGuiDataType_Double,
                                     &local_contrast_state.radius, &r_min,
                                     &r_max, "%.1f");
      changed |= ImGui::SliderScalar("Amount", ImGuiDataType_Double,
                                     &local_contrast_state.amount, &a_min,
                                     &a_max, "%.2f");

      if (changed) {
        Effect &e = get_or_create_effect(EffectType::LocalContrast);
        gegl_node_set(e.node, "radius", (gdouble)local_contrast_state.radius,
                      "amount", (gdouble)local_contrast_state.amount, NULL);
        apply_gegl_texture();
      }
      g_free(properties);
    } else {
      ImGui::TextDisabled("gegl:local-contrast not available on this build.");
      if (properties)
        g_free(properties);
    }
    ImGui::TreePop();
  }
}
