#include "Application/image.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include <SDL3/SDL.h>
#include <stdio.h>  // printf, fprintf
#include <stdlib.h> // abort

int main(int, char **) {
  // Setup SDL
  // [If using SDL_MAIN_USE_CALLBACKS: all code below until the main loop starts
  // would likely be your SDL_AppInit() function]
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    printf("Error: SDL_Init(): %s\n", SDL_GetError());
    return 1;
  }

  // Create SDL window graphics context
  float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  SDL_WindowFlags window_flags =
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  SDL_Window *window = SDL_CreateWindow("Gredit", (int)(1280 * main_scale),
                                        (int)(800 * main_scale), window_flags);
  if (window == nullptr) {
    printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(window);

  // Create GPU Device
  SDL_GPUDevice *gpu_device = SDL_CreateGPUDevice(
      SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
          SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB,
      true, nullptr);
  if (gpu_device == nullptr) {
    printf("Error: SDL_CreateGPUDevice(): %s\n", SDL_GetError());
    return 1;
  }
  printf("GPU driver: %s\n", SDL_GetGPUDeviceDriver(gpu_device));

  // Claim window for GPU Device
  if (!SDL_ClaimWindowForGPUDevice(gpu_device, window)) {
    printf("Error: SDL_ClaimWindowForGPUDevice(): %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetGPUSwapchainParameters(gpu_device, window,
                                SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                SDL_GPU_PRESENTMODE_VSYNC);

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  // ImGui::StyleColorsLight();

  // Setup scaling
  ImGuiStyle &style = ImGui::GetStyle();
  style.ScaleAllSizes(
      main_scale); // Bake a fixed style scale. (until we have a solution for
                   // dynamic style scaling, changing this requires resetting
                   // Style + calling this again)
  style.FontScaleDpi =
      main_scale; // Set initial font scale. (in docking branch: using
                  // io.ConfigDpiScaleFonts=true automatically overrides this
                  // for every window depending on the current monitor)

  // Setup Platform/Renderer backends
  ImGui_ImplSDL3_InitForSDLGPU(window);
  ImGui_ImplSDLGPU3_InitInfo init_info = {};
  init_info.Device = gpu_device;
  init_info.ColorTargetFormat =
      SDL_GetGPUSwapchainTextureFormat(gpu_device, window);
  init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
  ImGui_ImplSDLGPU3_Init(&init_info);

  style.FontSizeBase = 20.0f;
  ImFont *font = io.Fonts->AddFontFromFileTTF("./Quantico-Regular.ttf");
  IM_ASSERT(font != nullptr);

  bool show_demo_window = true;
  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  ImageManager manager = ImageManager(gpu_device, "./test/");
  manager.index = 0;
  Image *my_image = manager.loadImage();

  // Main loop
  bool done = false;
  while (!done) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT)
        done = true;
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
          event.window.windowID == SDL_GetWindowID(window))
        done = true;
    }

    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
      SDL_Delay(10);
      continue;
    }

    // Start the Dear ImGui frame
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();

    ImGui::NewFrame();

    if (show_demo_window)
      ImGui::ShowDemoWindow(&show_demo_window);

    manager.drawManager(&io);
    // Rendering
    ImGui::Render();
    ImDrawData *draw_data = ImGui::GetDrawData();
    const bool is_minimized =
        (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);

    SDL_GPUCommandBuffer *command_buffer =
        SDL_AcquireGPUCommandBuffer(gpu_device); // Acquire a GPU command buffer

    SDL_GPUTexture *swapchain_texture;
    SDL_WaitAndAcquireGPUSwapchainTexture(
        command_buffer, window, &swapchain_texture, nullptr,
        nullptr); // Acquire a swapchain texture

    if (swapchain_texture != nullptr && !is_minimized) {
      // This is mandatory: call ImGui_ImplSDLGPU3_PrepareDrawData() to upload
      // the vertex/index buffer!
      ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);

      // Setup and start a render pass
      SDL_GPUColorTargetInfo target_info = {};
      target_info.texture = swapchain_texture;
      target_info.clear_color = SDL_FColor{clear_color.x, clear_color.y,
                                           clear_color.z, clear_color.w};
      target_info.load_op = SDL_GPU_LOADOP_CLEAR;
      target_info.store_op = SDL_GPU_STOREOP_STORE;
      target_info.mip_level = 0;
      target_info.layer_or_depth_plane = 0;
      target_info.cycle = false;
      SDL_GPURenderPass *render_pass =
          SDL_BeginGPURenderPass(command_buffer, &target_info, 1, nullptr);

      // Render ImGui
      ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, render_pass);

      SDL_EndGPURenderPass(render_pass);
    }

    // Submit the command buffer
    SDL_SubmitGPUCommandBuffer(command_buffer);
  }

  // Cleanup
  SDL_WaitForGPUIdle(gpu_device);
  ImGui_ImplSDL3_Shutdown();
  ImGui_ImplSDLGPU3_Shutdown();
  ImGui::DestroyContext();

  SDL_ReleaseWindowFromGPUDevice(gpu_device, window);
  SDL_DestroyGPUDevice(gpu_device);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
