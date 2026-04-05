#ifndef IMAGE_EDITOR
#define IMAGE_EDITOR

#include <SDL3/SDL.h>
#include <filesystem>
#include <gegl.h>
#include <imgui.h>
#include <vector>

struct BrightnessContrastState {
  double contrast = 1.0;
  double brightness = 0.0;
};

enum class EffectType { BrightnessContrast };

struct Effect {
  GeglNode *node = nullptr;
  EffectType type;
};

Effect &get_or_create_effect(EffectType type);

class ImageEditor {
public:
  ImageEditor(SDL_GPUDevice *device) : device(device) {};
  ~ImageEditor();
  SDL_GPUTexture *preview_texture = nullptr;
  std::filesystem::path image_path;
  int width = 0;
  int height = 0;
  void load_path(std::filesystem::path);
  void render_preview();
  float get_zoom() const { return zoom; }
  ImVec2 get_pan() const { return pan; }
  void set_view(float next_zoom, ImVec2 next_pan) {
    zoom = next_zoom;
    pan = next_pan;
  }
  void render_controls();

private:
  SDL_GPUDevice *device = nullptr;
  float zoom = 0.0f;
  ImVec2 canvas_size = ImVec2(0.0f, 0.0f);
  ImVec2 pan = ImVec2(0.0f, 0.0f);
  void reset_view_to_image();
  void prepare_gegl_graph();
  void apply_gegl_texture();
  BrightnessContrastState brightness_contrast_state;
  void *image_src = nullptr;

  std::vector<Effect> effects;
  Effect &get_or_create_effect(EffectType type);

  GeglBuffer *image_buffer = nullptr;
  GeglNode *graph = nullptr;
  GeglNode *sink = nullptr;
  GeglNode *source = nullptr;
  GeglNode *last_node = nullptr;
};

#endif // !IMAGE_EDITOR
