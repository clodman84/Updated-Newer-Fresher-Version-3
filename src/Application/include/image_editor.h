#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <gegl.h>
#include <imgui.h>
#include <thread>
#include <vector>

struct ExposureState {
  double black_level = 0.0;
  double exposure = 0.0;
};

struct LevelsState {
  double in_low = 0.0;
  double in_high = 1.0;
  double gamma = 1.0;
  double out_low = 0.0;
  double out_high = 1.0;

  double in_low_r = 0.0, in_high_r = 1.0, gamma_r = 1.0, out_low_r = 0.0,
         out_high_r = 1.0;
  double in_low_g = 0.0, in_high_g = 1.0, gamma_g = 1.0, out_low_g = 0.0,
         out_high_g = 1.0;
  double in_low_b = 0.0, in_high_b = 1.0, gamma_b = 1.0, out_low_b = 0.0,
         out_high_b = 1.0;
  double in_low_a = 0.0, in_high_a = 1.0, gamma_a = 1.0, out_low_a = 0.0,
         out_high_a = 1.0;
};

struct ColorTemperatureState {
  double original_temperature = 6500.0;
  double intended_temperature = 6500.0;
};

struct HueChromaState {
  double hue = 0.0;
  double chroma = 0.0;
  double lightness = 0.0;
};

struct SaturationState {
  double scale = 1.0;
};

struct ColorEnhanceState {
  bool enabled = false;
};

struct SepiaState {
  double scale = 1.0;
};

struct UnsharpMaskState {
  double std_dev = 3.0;
  double scale = 0.5;
  double threshold = 0.0;
};

struct NoiseReductionState {
  int iterations = 4;
};

struct MonoMixerState {
  double red = 0.333;
  double green = 0.334;
  double blue = 0.333;
  bool preserve_luminosity = true;
};

enum class EffectType {
  Exposure,
  Levels,
  ColorTemperature,
  HueChroma,
  Saturation,
  ColorEnhance,
  Sepia,
  MonoMixer,
  UnsharpMask,
  NoiseReduction,
};

struct Effect {
  GeglNode *node = nullptr;
  EffectType type;
};

struct RenderRequest {
  GeglRectangle roi;
  float zoom;
};

Effect &get_or_create_effect(EffectType type);

class ImageEditor {
public:
  ImageEditor(SDL_GPUDevice *device) : device(device) {
    start_render_thread();
  };
  ~ImageEditor();
  std::filesystem::path image_path;
  int image_width = 0;
  int image_height = 0;

  int current_texture_offset_x = 0;
  int current_texture_offset_y = 0;
  int current_texture_width = 0;
  int current_texture_height = 0;

  void load_path(std::filesystem::path);
  void render_preview();
  float get_zoom() const { return zoom; }
  ImVec2 get_pan() const { return pan; }
  void set_view(float next_zoom, ImVec2 next_pan) {
    zoom = next_zoom;
    pan = next_pan;
  }
  void render_controls();
  void reset_view_to_image();
  void cleanup_stale_resources();

  GeglRectangle roi;

private:
  std::vector<SDL_GPUTexture *> textures_to_release;
  SDL_GPUDevice *device;
  SDL_GPUTexture *preview_texture = nullptr;

  float zoom = 1.0f;
  ImVec2 canvas_size = ImVec2(0.0f, 0.0f);
  ImVec2 pan = ImVec2(0.0f, 0.0f);
  void prepare_gegl_graph();

  void start_render_thread();
  void stop_render_thread();

  void apply_gegl_texture(RenderRequest req);
  void put_render_request();

  std::thread render_thread;
  std::atomic<bool> running{false};
  std::mutex request_mutex;
  std::condition_variable request_cv;
  RenderRequest latest_request;
  bool has_request{false};

  void remove_effect(EffectType type);
  bool is_effect_active(EffectType type) const;

  ExposureState exposure_state;
  LevelsState levels_state;
  ColorTemperatureState color_temperature_state;
  HueChromaState hue_chroma_state;
  SaturationState saturation_state;
  ColorEnhanceState color_enhance_state;
  SepiaState sepia_state;
  MonoMixerState mono_mixer_state;
  UnsharpMaskState unsharp_mask_state;
  NoiseReductionState noise_reduction_state;

  void *image_src = nullptr;

  std::vector<Effect> effects;
  Effect &get_or_create_effect(EffectType type);

  GeglBuffer *image_buffer = nullptr;
  GeglNode *graph = nullptr;
  GeglNode *sink = nullptr;
  GeglNode *source = nullptr;
  GeglNode *last_node = nullptr;
};
