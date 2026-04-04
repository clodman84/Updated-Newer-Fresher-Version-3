#include "SDL3/SDL_gpu.h"
#include "application.h"

ImageEditor::~ImageEditor() {
  if (preview_texture != nullptr)
    SDL_ReleaseGPUTexture(device, preview_texture);
}
