#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <filesystem>
#include <functional>
#include <imgui.h>

class Image {
public:
  Image(std::filesystem::path filename, SDL_GPUDevice *device)
      : filename(filename), device(device) {};
  ~Image();

  bool is_valid() const;

  void load_thumbnail();
  void load_fullres();
  void load_halfres();

  void destroy_texture();
  void destroy_thumbnail();

  SDL_GPUTexture *texture = nullptr;
  SDL_GPUTexture *thumbnail_texture = nullptr;

  int width = 0;
  int height = 0;

  int thumb_width = 0;
  int thumb_height = 0;
  std::filesystem::path filename;

  void
  render_thumbnail(float height,
                   const std::function<void(const std::string &)> &on_click,
                   bool is_selected, bool is_active, bool is_highlight) const {
    // Helper function to render the thumbnail of an image, the image is
    // clickable
    float aspect = (float)thumb_width / thumb_height;
    ImVec2 size(height * aspect, height);
    ImVec2 p_min = ImGui::GetCursorScreenPos();
    ImVec2 p_max = ImVec2(p_min.x + size.x, p_min.y + size.y);

    if (thumbnail_texture)
      ImGui::Image(thumbnail_texture, size);

    ImGui::SetCursorScreenPos(p_min);
    if (ImGui::InvisibleButton("##hitbox", size)) {
      on_click(filename);
    }

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    if (is_selected || is_active) {
      draw_list->AddRect(p_min, p_max,
                         ImGui::GetColorU32(ImGuiCol_HeaderActive), 0.0f, 0,
                         3.0f);
    }
    if (is_selected && !is_highlight) {
      draw_list->AddRectFilled(p_min, p_max,
                               ImGui::GetColorU32(ImGuiCol_Header, 0.7f));
    }
    if (is_highlight) {
      draw_list->AddRectFilled(
          p_min, p_max,
          ImGui::GetColorU32({0.49411764705882355, 0.8274509803921568,
                              0.2823529411764706, 0.7f}));
    }
  }

private:
  SDL_GPUDevice *device;
};
