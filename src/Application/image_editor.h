#ifndef IMAGE_EDITOR
#define IMAGE_EDITOR

#include <SDL3/SDL.h>
#include <filesystem>
#include <gegl.h>
#include <imgui.h>
#include <vector>

// Tone & Exposure
struct BrightnessContrastState {
  double contrast = 1.0;
  double brightness = 0.0;
};

struct ExposureState {
  double black_level = 0.0;
  double exposure = 0.0;
};

struct ShadowsHighlightsState {
  double shadows = 0.0;
  double highlights = 0.0;
  double whitepoint = 0.0;
  double radius = 100.0;
};

struct LevelsState {
  double in_low = 0.0;
  double in_high = 1.0;
  double out_low = 0.0;
  double out_high = 1.0;
};

// Colour
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

struct StretchContrastState {
  bool enabled = false;
};

struct StretchContrastHSVState {
  bool enabled = false;
};

// Sharpening
struct UnsharpMaskState {
  double std_dev = 3.0;
  double scale = 0.5;
  double threshold = 0.0;
};

struct HighPassState {
  double std_dev = 4.0;
  double contrast = 1.0;
};

// Noise Reduction
struct NoiseReductionState {
  int iterations = 4;
};

struct SNNMeanState {
  int radius = 8;
  int pairs = 2;
};

struct DomainTransformState {
  double sigma_s = 30.0;
  double sigma_r = 0.4;
  int n_iterations = 3;
};

struct BilateralFilterState {
  double blur_radius = 3.0;
  double edge_preservation = 0.2;
  bool dirty = false;
};

// Local Contrast
struct LocalContrastState {
  double radius = 20.0;
  double amount = 1.0;
};

enum class EffectType {
  BrightnessContrast,
  // Tone & Exposure
  Exposure,
  ShadowsHighlights,
  Levels,
  // Colour
  ColorTemperature,
  HueChroma,
  Saturation,
  ColorEnhance,
  StretchContrast,
  StretchContrastHSV,
  // Sharpening
  UnsharpMask,
  HighPass,
  // Noise Reduction
  NoiseReduction,
  SNNMean,
  DomainTransform,
  BilateralFilter,
  // Local Contrast
  LocalContrast
};

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
  void cleanup_stale_resources();

private:
  std::vector<SDL_GPUTexture *> textures_to_release;
  SDL_GPUDevice *device = nullptr;
  float zoom = 0.0f;
  ImVec2 canvas_size = ImVec2(0.0f, 0.0f);
  ImVec2 pan = ImVec2(0.0f, 0.0f);
  void reset_view_to_image();
  void prepare_gegl_graph();
  void apply_gegl_texture();

  // Tone & Exposure
  BrightnessContrastState brightness_contrast_state;
  ExposureState exposure_state;
  ShadowsHighlightsState shadows_highlights_state;
  LevelsState levels_state;

  // Colour
  ColorTemperatureState color_temperature_state;
  HueChromaState hue_chroma_state;
  SaturationState saturation_state;
  ColorEnhanceState color_enhance_state;
  StretchContrastState stretch_contrast_state;
  StretchContrastHSVState stretch_contrast_hsv_state;

  // Sharpening
  UnsharpMaskState unsharp_mask_state;
  HighPassState high_pass_state;

  // Noise Reduction
  NoiseReductionState noise_reduction_state;
  SNNMeanState snn_mean_state;
  DomainTransformState domain_transform_state;
  BilateralFilterState bilateral_filter_state;

  // Local Contrast
  LocalContrastState local_contrast_state;

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
