#include "SDL3/SDL_gpu.h"
#include "application.h"

ImageEditor::~ImageEditor() {
  if (preview_texture != nullptr)
    SDL_ReleaseGPUTexture(device, preview_texture);
}

void ImageEditor::load_path(std::filesystem::path path) {
#ifdef TRACY_ENABLE
  ZoneScopedN("load_path");
#endif
  image_path = std::move(path);
  int src_w = 0;
  int src_h = 0;
  unsigned char *src = load_texture_data_from_file(image_path, &src_w, &src_h);

  constexpr int dst_h = 1024;
  const float factor = static_cast<float>(dst_h) / src_h;
  const int dst_w = std::max(1, static_cast<int>(src_w * factor));

  unsigned char *dst = static_cast<unsigned char *>(
      IM_ALLOC(static_cast<size_t>(dst_w) * dst_h * 4));
  stbir_resize_uint8_linear(src, src_w, src_h, 0, dst, dst_w, dst_h, 0,
                            STBIR_RGBA);
  stbi_image_free(src);

  SDL_GPUTexture *texture = nullptr;
  upload_texture_data_to_gpu(dst, dst_w, dst_h, device, &texture, false);
  if (preview_texture != nullptr) {
    SDL_ReleaseGPUTexture(device, preview_texture);
  }

  preview_texture = texture;
  width = dst_w;
  height = dst_h;
  reset_view_to_image();
}

bool DrawGeglBrightnessContrastNode(BrightnessContrastState &s) {
  bool changed = false;

  if (!ImGui::TreeNodeEx("Brightness / Contrast",
                         ImGuiTreeNodeFlags_DefaultOpen |
                             ImGuiTreeNodeFlags_SpanAvailWidth))
    return false;

  ImGui::PushID("gegl:brightness-contrast");

  float c = (float)s.contrast;
  if (ImGui::SliderFloat("Contrast", &c, 0.0f, 2.0f, "%.3f")) {
    s.contrast = std::clamp<double>(c, -5.0, 5.0);
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("R##c")) {
    s.contrast = 1.0;
    changed = true;
  }

  float b = (float)s.brightness;
  if (ImGui::SliderFloat("Brightness", &b, -1.0f, 1.0f, "%.3f")) {
    s.brightness = std::clamp<double>(b, -3.0, 3.0);
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("R##b")) {
    s.brightness = 0.0;
    changed = true;
  }

  ImGui::PopID();
  ImGui::TreePop();

  return changed;
}

void ImageEditor::render_controls() {
  DrawGeglBrightnessContrastNode(brightness_contrast_state);
}
