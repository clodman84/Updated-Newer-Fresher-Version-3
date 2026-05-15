#include "include/IconsFontAwesome6.h"
#include "include/image_editor.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

void ImageEditor::render_preview() {

  ImGui::TableNextColumn();
  ImGui::BeginChild("PreviewChild", ImVec2(0, 0));

  ImGui::Text(ICON_FA_WAND_MAGIC_SPARKLES);
  ImGui::SameLine();

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));

  if (ImGui::SmallButton(ICON_FA_COMPRESS))
    reset_view_to_image();

  ImGui::PopStyleColor(3);

  ImGui::Separator();

  ImTextureRef texture_id = preview_texture;
  const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
  canvas_size = ImGui::GetContentRegionAvail();

  if (preview_texture == nullptr) {
    printf("Preview Texture is NULL pointer!\n");
    ImGui::TextUnformatted("Bruh Moment");
    ImGui::EndChild();
    return;
  }

  ImGui::InvisibleButton("canvas", canvas_size,
                         ImGuiButtonFlags_MouseButtonLeft);
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();

  auto io = ImGui::GetIO();
  if (hovered && io.MouseWheel != 0.0f) {
    const float old_zoom = zoom;
    zoom *= powf(1.1f, io.MouseWheel);
    zoom = std::clamp(zoom, 0.1f, 20.0f);

    ImVec2 mouse_local;
    mouse_local.x = io.MousePos.x - canvas_pos.x - pan.x;
    mouse_local.y = io.MousePos.y - canvas_pos.y - pan.y;
    const float scale = zoom / old_zoom - 1.0f;
    pan.x -= mouse_local.x * scale;
    pan.y -= mouse_local.y * scale;
  }

  if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    pan.x += io.MouseDelta.x;
    pan.y += io.MouseDelta.y;
  }

  ImVec2 image_size = {image_width * zoom, image_height * zoom};
  if (image_size.x <= canvas_size.x) {
    pan.x = (canvas_size.x - image_size.x) * 0.5f;
  } else {
    pan.x = std::clamp(pan.x, canvas_size.x - image_size.x, 0.0f);
  }
  if (image_size.y <= canvas_size.y) {
    pan.y = (canvas_size.y - image_size.y) * 0.5f;
  } else {
    pan.y = std::clamp(pan.y, canvas_size.y - image_size.y, 0.0f);
  }

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  draw_list->PushClipRect(
      canvas_pos,
      ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), true);
  const ImVec2 image_pos = {canvas_pos.x + pan.x, canvas_pos.y + pan.y};

  // Compute roi in image-space (unscaled) pixels: the visible portion of the
  // full image, clamped to [0, image_width] x [0, image_height].
  roi.x = (int)std::max(0.0f, (canvas_pos.x - image_pos.x) / zoom);
  roi.y = (int)std::max(0.0f, (canvas_pos.y - image_pos.y) / zoom);

  int roi_x2 = (int)std::min(
      (float)image_width, (canvas_pos.x + canvas_size.x - image_pos.x) / zoom);
  int roi_y2 = (int)std::min(
      (float)image_height, (canvas_pos.y + canvas_size.y - image_pos.y) / zoom);

  roi.width = roi_x2 - roi.x;
  roi.height = roi_y2 - roi.y;

  // TODO: This is a rather naive and stupid way to do things, makes the UI
  // unresponsive, instead maintain some sort of queueing system
  if (roi.width != current_texture_width ||
      roi.height != current_texture_height ||
      roi.x != current_texture_offset_x || roi.y != current_texture_offset_y)
    put_render_request();

  ImVec2 roi_pos = {image_pos.x + current_texture_offset_x * zoom,
                    image_pos.y + current_texture_offset_y * zoom};

  draw_list->AddImage(texture_id, roi_pos,
                      ImVec2(roi_pos.x + current_texture_width * zoom,
                             roi_pos.y + current_texture_height * zoom),
                      ImVec2(0, 0), ImVec2(1, 1));

  draw_list->PopClipRect();
  ImGui::EndChild();
}
static bool AccentSliderDouble(const char *label, double &v, double v_min,
                               double v_max, const char *fmt,
                               ImU32 accent = IM_COL32(82, 130, 255, 255)) {
  ImGui::PushID(label);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  float W = ImGui::GetContentRegionAvail().x;
  float H = 20.0f;
  float rounding = 3.0f;

  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##sl", ImVec2(W, H));
  bool hovered = ImGui::IsItemHovered();
  bool active = ImGui::IsItemActive();

  if (active && ImGui::IsMouseDragging(0)) {
    double delta = (double)ImGui::GetIO().MouseDelta.x / W * (v_max - v_min);
    v = std::clamp(v + delta, v_min, v_max);
  }

  float t = (float)((v - v_min) / (v_max - v_min));

  // Extract RGB from accent for vivid fills
  float ar = ((accent >> 0) & 0xFF) / 255.0f;
  float ag = ((accent >> 8) & 0xFF) / 255.0f;
  float ab = ((accent >> 16) & 0xFF) / 255.0f;

  ImU32 fill_dim = IM_COL32((int)(ar * 255 * 0.9f), (int)(ag * 255 * 0.9f),
                            (int)(ab * 255 * 0.9f), 180);
  ImU32 fill_subtle = IM_COL32((int)(ar * 255 * 0.7f), (int)(ag * 255 * 0.7f),
                               (int)(ab * 255 * 0.7f), 100);

  // track bg
  dl->AddRectFilled(p, ImVec2(p.x + W, p.y + H), IM_COL32(35, 35, 35, 255),
                    rounding);
  // colored fill
  ImU32 fill = active ? fill_dim : fill_subtle;
  if (t > 0.0f)
    dl->AddRectFilled(p, ImVec2(p.x + t * W, p.y + H), fill, rounding);
  // thumb
  float tx = p.x + t * W;
  dl->AddLine(ImVec2(tx, p.y + 2), ImVec2(tx, p.y + H - 2),
              active ? IM_COL32(255, 255, 255, 255)
                     : IM_COL32(210, 210, 210, 180),
              2.0f);

  // label + value
  char val_buf[32];
  snprintf(val_buf, sizeof(val_buf), fmt, v);
  float text_y = p.y + (H - ImGui::GetTextLineHeight()) * 0.5f;
  dl->AddText(ImVec2(p.x + 7, text_y), IM_COL32(220, 220, 220, 240), label);
  ImVec2 vsz = ImGui::CalcTextSize(val_buf);
  dl->AddText(ImVec2(p.x + W - vsz.x - 7, text_y),
              active ? IM_COL32(255, 255, 255, 255)
                     : IM_COL32(200, 200, 200, 220),
              val_buf);

  ImGui::PopID();
  return active && ImGui::IsMouseDragging(0);
}

static bool draw_levels_bar(const char *id, double &in_low, double &gamma,
                            double &in_high, double &out_low, double &out_high,
                            ImVec4 color_left, ImVec4 color_right) {
  ImGui::PushID(id);
  ImDrawList *dl = ImGui::GetWindowDrawList();

  float width = ImGui::GetContentRegionAvail().x - 20;
  float height = 20.0f;
  bool changed = false;

  ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImVec2 p1 = ImVec2(p0.x + width, p0.y + height);

  auto remap = [&](float t) -> float {
    if (t <= in_low)
      return 0.0f;
    if (t >= in_high)
      return 1.0f;
    float x = (float)((t - in_low) / (in_high - in_low));
    x = powf(x, (float)(1.0 / gamma));
    return (float)(out_low + x * (out_high - out_low));
  };

  const int STEPS = 64;
  for (int i = 0; i < STEPS; i++) {
    float t0 = (float)i / STEPS;
    float t1 = (float)(i + 1) / STEPS;
    ImVec4 c0 = ImLerp(color_left, color_right, remap(t0));
    ImVec4 c1 = ImLerp(color_left, color_right, remap(t1));
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

  ImGui::InvisibleButton("input_bar", ImVec2(width, height + 16));
  bool active = ImGui::IsItemActive();

  float mouse_t =
      std::clamp((ImGui::GetIO().MousePos.x - p0.x) / width, 0.0f, 1.0f);

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
    if (target == LOW)
      in_low = std::min((double)mouse_t, in_high - 0.001);
    else if (target == HIGH)
      in_high = std::max((double)mouse_t, in_low + 0.001);
    else
      gamma = std::clamp(t_to_gamma(mouse_t), 0.1, 10.0);
    changed = true;
  }

  auto X = [&](float t) { return p0.x + t * width; };

  auto draw_tri = [&](float t, bool hovered) {
    float x = X(t);
    float by = p1.y;
    ImU32 col =
        hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 200, 220);
    dl->AddTriangleFilled(ImVec2(x - 6, by + 2), ImVec2(x + 6, by + 2),
                          ImVec2(x, by - 6), IM_COL32(0, 0, 0, 120));
    dl->AddTriangleFilled(ImVec2(x - 5, by + 1), ImVec2(x + 5, by + 1),
                          ImVec2(x, by - 5), col);
  };

  auto draw_diamond = [&](float t, bool hovered) {
    float x = X(t);
    float cy = (p0.y + p1.y) * 0.5f;
    ImU32 col =
        hovered ? IM_COL32(100, 180, 255, 255) : IM_COL32(180, 180, 180, 220);
    dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), IM_COL32(255, 255, 255, 40),
                1.0f);
    dl->AddQuadFilled(ImVec2(x, -6 + cy), ImVec2(x + 6, cy), ImVec2(x, 6 + cy),
                      ImVec2(x - 6, cy), IM_COL32(0, 0, 0, 100));
    dl->AddQuadFilled(ImVec2(x, -5 + cy), ImVec2(x + 5, cy), ImVec2(x, 5 + cy),
                      ImVec2(x - 5, cy), col);
  };

  draw_tri(t_low, active && target == LOW);
  draw_tri(t_high, active && target == HIGH);
  draw_diamond(t_gamma, active && target == MID);

  ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y + 14));

  ImVec2 p2 = ImGui::GetCursorScreenPos();
  ImVec2 p3 = ImVec2(p2.x + width, p2.y + height);

  dl->AddRectFilledMultiColor(
      p2, p3, IM_COL32(0, 0, 0, 255), IM_COL32(255, 255, 255, 255),
      IM_COL32(255, 255, 255, 255), IM_COL32(0, 0, 0, 255));

  ImGui::InvisibleButton("output_bar", ImVec2(width, height + 14));
  active = ImGui::IsItemActive();

  mouse_t = std::clamp((ImGui::GetIO().MousePos.x - p2.x) / width, 0.0f, 1.0f);
  float d_ol = fabsf(mouse_t - (float)out_low);
  float d_oh = fabsf(mouse_t - (float)out_high);
  bool out_low_target = d_ol < d_oh;

  if (active && ImGui::IsMouseDragging(0)) {
    if (out_low_target)
      out_low = std::min((double)mouse_t, out_high - 0.001);
    else
      out_high = std::max((double)mouse_t, out_low + 0.001);
    changed = true;
  }

  auto draw_tri_out = [&](float t, bool hovered) {
    float x = p2.x + t * width;
    float by = p3.y;
    ImU32 col =
        hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 200, 220);
    dl->AddTriangleFilled(ImVec2(x - 6, by + 2), ImVec2(x + 6, by + 2),
                          ImVec2(x, by - 6), IM_COL32(0, 0, 0, 120));
    dl->AddTriangleFilled(ImVec2(x - 5, by + 1), ImVec2(x + 5, by + 1),
                          ImVec2(x, by - 5), col);
  };

  draw_tri_out((float)out_low, active && out_low_target);
  draw_tri_out((float)out_high, active && !out_low_target);

  ImGui::SetCursorScreenPos(ImVec2(p2.x, p3.y + 12));
  ImGui::Dummy(ImVec2(0, 4));

  ImGui::PopID();
  return changed;
}

enum class EffectHeaderAction {
  None,
  Toggled,
  Reset,
};

static EffectHeaderAction draw_effect_header(bool &active, const char *label,
                                             const char *tooltip,
                                             bool &out_open) {
  ImGuiTreeNodeFlags flags =
      ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanAvailWidth;
  out_open = ImGui::TreeNodeEx(label, flags);

  const ImGuiStyle &style = ImGui::GetStyle();
  float w_eye = ImGui::CalcTextSize(ICON_FA_EYE).x + style.FramePadding.x * 2;
  float w_reset =
      ImGui::CalcTextSize(ICON_FA_ROTATE_LEFT).x + style.FramePadding.x * 2;
  float w_question = ImGui::CalcTextSize(ICON_FA_CIRCLE_QUESTION).x;
  float total = w_eye + w_reset + w_question + style.ItemSpacing.x * 2;
  float offset = ImGui::GetWindowWidth() - total - style.WindowPadding.x -
                 (ImGui::GetScrollMaxY() > 0 ? style.ScrollbarSize : 0.0f);

  ImGui::SameLine(offset);

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, .5f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, .8f));
  ImGui::PushStyleColor(ImGuiCol_Text, active ? ImVec4(1.f, 0.84f, 0.f, 1.f)
                                              : ImVec4(1.f, 1.f, 1.f, 0.6f));

  EffectHeaderAction action = EffectHeaderAction::None;
  ImGui::PushID(label);
  if (ImGui::SmallButton(active ? ICON_FA_TOGGLE_ON : ICON_FA_TOGGLE_OFF)) {
    active = !active;
    action = EffectHeaderAction::Toggled;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton(ICON_FA_ROTATE_LEFT))
    action = EffectHeaderAction::Reset;

  ImGui::SameLine();
  ImGui::TextDisabled(ICON_FA_CIRCLE_QUESTION);
  if (ImGui::BeginItemTooltip()) {
    ImGui::PushTextWrapPos(300.f);
    ImGui::TextUnformatted(tooltip);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
  ImGui::PopID();
  ImGui::PopStyleColor(4);
  return action;
}

void ImageEditor::toggle_effect(EffectType type, bool now_active) {
  if (now_active)
    get_or_create_effect(type);
  else
    remove_effect(type);
  put_render_request();
}

void ImageEditor::render_controls() {
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 6));

  // ── Tone & Exposure ───────────────────────────────────────
  ImGui::SeparatorText(ICON_FA_SUN "  Tone & Exposure");

  if (gegl_has_operation("gegl:exposure")) {
    const EffectType type = EffectType::Exposure;
    bool active = is_effect_active(type);
    bool open = false;
    switch (draw_effect_header(
        active, "Exposure",
        "Exposure adjustment in the linear light domain — applies a "
        "black-level offset and an exposure value in stops.",
        open)) {
    case EffectHeaderAction::Toggled:
      toggle_effect(type, active);
      break;
    case EffectHeaderAction::Reset:
      exposure_state = ExposureState();
      if (active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "black-level",
                      (gdouble)exposure_state.black_level, "exposure",
                      (gdouble)exposure_state.exposure, NULL);
        put_render_request();
      }
      break;
    case EffectHeaderAction::None:
      break;
    }
    if (open) {
      bool changed = false;
      changed |= AccentSliderDouble("Black Level", exposure_state.black_level,
                                    -0.1, 0.1, "%.3f");
      changed |= AccentSliderDouble("Exposure (EV)", exposure_state.exposure,
                                    -10.0, 10.0, "%.2f");
      if (changed && active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "black-level",
                      (gdouble)exposure_state.black_level, "exposure",
                      (gdouble)exposure_state.exposure, NULL);
        put_render_request();
      }
      ImGui::TreePop();
    }
  }

  {
    const EffectType type = EffectType::Levels;
    bool active = is_effect_active(type);
    bool open = false;

    // Pushes the full levels state to the gegl node — used on both reset and
    // slider change.
    auto apply_levels = [&] {
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
      put_render_request();
    };

    switch (draw_effect_header(
        active, "Levels",
        "Per-channel input/output levels with gamma midtone adjustment.",
        open)) {
    case EffectHeaderAction::Toggled:
      toggle_effect(type, active);
      break;
    case EffectHeaderAction::Reset:
      levels_state = LevelsState();
      if (active)
        apply_levels();
      break;
    case EffectHeaderAction::None:
      break;
    }
    if (open) {
      bool changed = false;

      ImGui::SeparatorText("Composite");
      changed |= draw_levels_bar("C", levels_state.in_low, levels_state.gamma,
                                 levels_state.in_high, levels_state.out_low,
                                 levels_state.out_high, ImVec4(0, 0, 0, 1),
                                 ImVec4(1, 1, 1, 1));

      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
      ImGui::SeparatorText("Red");
      ImGui::PopStyleColor();
      changed |= draw_levels_bar(
          "R", levels_state.in_low_r, levels_state.gamma_r,
          levels_state.in_high_r, levels_state.out_low_r,
          levels_state.out_high_r, ImVec4(0, 0, 0, 1), ImVec4(1, 0, 0, 1));

      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.0f, 0.5f, 1.0f));
      ImGui::SeparatorText("Green");
      ImGui::PopStyleColor();
      changed |= draw_levels_bar(
          "G", levels_state.in_low_g, levels_state.gamma_g,
          levels_state.in_high_g, levels_state.out_low_g,
          levels_state.out_high_g, ImVec4(0, 0, 0, 1), ImVec4(0, 1, 0, 1));

      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 1.0f, 1.0f));
      ImGui::SeparatorText("Blue");
      ImGui::PopStyleColor();
      changed |= draw_levels_bar(
          "B", levels_state.in_low_b, levels_state.gamma_b,
          levels_state.in_high_b, levels_state.out_low_b,
          levels_state.out_high_b, ImVec4(0, 0, 0, 1), ImVec4(0, 0, 1, 1));

      if (changed && active)
        apply_levels();
      ImGui::TreePop();
    }
  }

  // ── Colour ────────────────────────────────────────────────
  ImGui::SeparatorText(ICON_FA_DROPLET "  Colour");

  if (gegl_has_operation("gegl:color-temperature")) {
    const EffectType type = EffectType::ColorTemperature;
    bool active = is_effect_active(type);
    bool open = false;
    switch (draw_effect_header(
        active, "Color Temperature",
        "Changes colour temperature. Both values in Kelvin — "
        "lower is warmer (orange), higher is cooler (blue).",
        open)) {
    case EffectHeaderAction::Toggled:
      toggle_effect(type, active);
      break;
    case EffectHeaderAction::Reset:
      color_temperature_state = ColorTemperatureState();
      if (active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "original-temperature",
                      (gdouble)color_temperature_state.original_temperature,
                      "intended-temperature",
                      (gdouble)color_temperature_state.intended_temperature,
                      NULL);
        put_render_request();
      }
      break;
    case EffectHeaderAction::None:
      break;
    }
    if (open) {
      bool changed = false;
      changed |= AccentSliderDouble(
          "Original", color_temperature_state.original_temperature, 1000.0,
          12000.0, "%.0f K", IM_COL32(255, 160, 80, 255));
      changed |= AccentSliderDouble(
          "Intended", color_temperature_state.intended_temperature, 1000.0,
          12000.0, "%.0f K", IM_COL32(80, 160, 255, 255));
      if (changed && active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "original-temperature",
                      (gdouble)color_temperature_state.original_temperature,
                      "intended-temperature",
                      (gdouble)color_temperature_state.intended_temperature,
                      NULL);
        put_render_request();
      }
      ImGui::TreePop();
    }
  }

  if (gegl_has_operation("gegl:hue-chroma")) {
    const EffectType type = EffectType::HueChroma;
    bool active = is_effect_active(type);
    bool open = false;
    switch (draw_effect_header(
        active, "Hue-Chroma",
        "Adjusts hue, chroma (saturation), and lightness in a perceptually "
        "uniform colour space. Cleaner than naive HSL, especially when "
        "pushing chroma.",
        open)) {
    case EffectHeaderAction::Toggled:
      toggle_effect(type, active);
      break;
    case EffectHeaderAction::Reset:
      hue_chroma_state = HueChromaState();
      if (active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "hue", (gdouble)hue_chroma_state.hue, "chroma",
                      (gdouble)hue_chroma_state.chroma, "lightness",
                      (gdouble)hue_chroma_state.lightness, NULL);
        put_render_request();
      }
      break;
    case EffectHeaderAction::None:
      break;
    }
    if (open) {
      bool changed = false;
      changed |= AccentSliderDouble("Hue", hue_chroma_state.hue, -180.0, 180.0,
                                    "%.1f°");
      changed |= AccentSliderDouble("Chroma", hue_chroma_state.chroma, -100.0,
                                    100.0, "%.1f");
      changed |= AccentSliderDouble("Lightness", hue_chroma_state.lightness,
                                    -100.0, 100.0, "%.1f");
      if (changed && active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "hue", (gdouble)hue_chroma_state.hue, "chroma",
                      (gdouble)hue_chroma_state.chroma, "lightness",
                      (gdouble)hue_chroma_state.lightness, NULL);
        put_render_request();
      }
      ImGui::TreePop();
    }
  }

  if (gegl_has_operation("gegl:color-enhance")) {
    const EffectType type = EffectType::ColorEnhance;
    bool active = is_effect_active(type);
    bool open = false;
    switch (draw_effect_header(
        active, "Color Enhance",
        "Stretches chroma to cover the maximum possible range, "
        "keeping hue and lightness untouched.",
        open)) {
    case EffectHeaderAction::Toggled:
      toggle_effect(type, active);
      break;
    case EffectHeaderAction::Reset:
      color_enhance_state = ColorEnhanceState();
      break;
    case EffectHeaderAction::None:
      break;
    }
    if (open)
      ImGui::TreePop();
  }

  if (gegl_has_operation("gegl:saturation")) {
    const EffectType type = EffectType::Saturation;
    bool active = is_effect_active(type);
    bool open = false;
    switch (draw_effect_header(
        active, "Saturation",
        "Scale multiplier on saturation. 1.0 = unchanged, 0.0 = "
        "desaturated, 2.0 = doubled.",
        open)) {
    case EffectHeaderAction::Toggled:
      toggle_effect(type, active);
      break;
    case EffectHeaderAction::Reset:
      saturation_state = SaturationState();
      if (active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "scale", (gdouble)saturation_state.scale, NULL);
        put_render_request();
      }
      break;
    case EffectHeaderAction::None:
      break;
    }
    if (open) {
      bool changed =
          AccentSliderDouble("Scale", saturation_state.scale, 0.0, 2.0, "%.2f");
      if (changed && active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "scale", (gdouble)saturation_state.scale, NULL);
        put_render_request();
      }
      ImGui::TreePop();
    }
  }

  if (gegl_has_operation("gegl:sepia")) {
    const EffectType type = EffectType::Sepia;
    bool active = is_effect_active(type);
    bool open = false;
    switch (draw_effect_header(
        active, "Sepia", "Apply a sepia tone to the input image.", open)) {
    case EffectHeaderAction::Toggled:
      toggle_effect(type, active);
      break;
    case EffectHeaderAction::Reset:
      sepia_state = SepiaState();
      if (active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "scale", (gdouble)sepia_state.scale, NULL);
        put_render_request();
      }
      break;
    case EffectHeaderAction::None:
      break;
    }
    if (open) {
      bool changed =
          AccentSliderDouble("Effect Strength", sepia_state.scale, 0.0, 1.0,
                             "%.2f", IM_COL32(180, 130, 80, 255));
      if (changed && active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "scale", (gdouble)sepia_state.scale, NULL);
        put_render_request();
      }
      ImGui::TreePop();
    }
  }

  if (gegl_has_operation("gegl:mono-mixer")) {
    const EffectType type = EffectType::MonoMixer;
    bool active = is_effect_active(type);
    bool open = false;
    switch (draw_effect_header(active, "Mono Mixer",
                               "Monochrome channel mixer.", open)) {
    case EffectHeaderAction::Toggled:
      toggle_effect(type, active);
      break;
    case EffectHeaderAction::Reset:
      mono_mixer_state = MonoMixerState();
      if (active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "red", (gdouble)mono_mixer_state.red, "green",
                      (gdouble)mono_mixer_state.green, "blue",
                      (gdouble)mono_mixer_state.blue, "preserve-luminosity",
                      (gboolean)mono_mixer_state.preserve_luminosity, NULL);
        put_render_request();
      }
      break;
    case EffectHeaderAction::None:
      break;
    }
    if (open) {
      bool changed = false;
      changed |= AccentSliderDouble("Red", mono_mixer_state.red, -2.0, 2.0,
                                    "%.3f", IM_COL32(200, 70, 70, 255));
      changed |= AccentSliderDouble("Green", mono_mixer_state.green, -2.0, 2.0,
                                    "%.3f", IM_COL32(70, 180, 80, 255));
      changed |= AccentSliderDouble("Blue", mono_mixer_state.blue, -2.0, 2.0,
                                    "%.3f", IM_COL32(70, 120, 210, 255));
      changed |= ImGui::Checkbox("Preserve Luminosity",
                                 &mono_mixer_state.preserve_luminosity);
      if (changed && active) {
        Effect &e = get_or_create_effect(type);
        gegl_node_set(e.node, "red", (gdouble)mono_mixer_state.red, "green",
                      (gdouble)mono_mixer_state.green, "blue",
                      (gdouble)mono_mixer_state.blue, "preserve-luminosity",
                      (gboolean)mono_mixer_state.preserve_luminosity, NULL);
        put_render_request();
      }
      ImGui::TreePop();
    }
  }

  ImGui::PopStyleVar();
}
