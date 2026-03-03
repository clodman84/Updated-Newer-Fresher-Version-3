#include "Application/application.h"
#include "SDL3/SDL_dialog.h"
#include "SDL3/SDL_log.h"
#include "SDL3/SDL_video.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include <SDL3/SDL.h>
#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdio.h>  // printf, fprintf
#include <stdlib.h> // abort

std::string get_folder_name(const char *full_path) {
  return std::filesystem::path(full_path).filename().string();
}

typedef struct FolderDialogData {
  Database *database;
  SDL_GPUDevice *device;
  std::deque<Session> *session_list;
  std::mutex *session_queue_mutex;
} FolderDialogData;

static const SDL_DialogFileFilter csv_filters[] = {{"CSV files", "csv"}};

static void SDLCALL mess_list_callback(void *userdata,
                                       const char *const *filelist,
                                       int filter) {
  if (!filelist) {
    SDL_Log("An error occured: %s", SDL_GetError());
    return;
  } else if (!*filelist) {
    SDL_Log("The user did not select any file.");
    SDL_Log("Most likely, the dialog was canceled.");
    return;
  }

  while (*filelist) {
    std::string filename(*filelist);
    SDL_Log("Full path to selected file: '%s'", *filelist);
    filelist++;
    Database *db = (Database *)userdata;
    db->read_csv(filename);
    db->show_loaded_csv = true;
  }

  if (filter < 0) {
    SDL_Log("The current platform does not support fetching "
            "the selected filter, or the user did not select"
            " any filter.");
  } else if (filter < SDL_arraysize(csv_filters)) {
    SDL_Log("The filter selected by the user is '%s' (%s).",
            csv_filters[filter].pattern, csv_filters[filter].name);
  }
}

static void SDLCALL load_roll_callback(void *userdata,
                                       const char *const *filelist,
                                       int filter) {

  std::unique_ptr<FolderDialogData> data(
      static_cast<FolderDialogData *>(userdata));

  if (!filelist) {
    SDL_Log("An error occured: %s", SDL_GetError());
    return;
  } else if (!*filelist) {
    SDL_Log("The user did not select any file.");
    SDL_Log("Most likely, the dialog was canceled.");
    return;
  }

  while (*filelist) {
    std::string path(*filelist);
    SDL_Log("Full path to selected folder: '%s'", *filelist);
    filelist++;
    FolderDialogData *data = (FolderDialogData *)userdata;

    Database *db = data->database;
    SDL_GPUDevice *device = data->device;
    std::deque<Session> *sessions = data->session_list;
    std::mutex *session_queue_mutex = data->session_queue_mutex;

    Session new_session = Session(data->database, path, data->device);

    std::lock_guard lock(*session_queue_mutex);
    data->session_list->emplace_back(new_session);
  }
}

int main(int, char **) {
  // SDL UI Related Shenanigans
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    printf("Error: SDL_Init(): %s\n", SDL_GetError());
    return 1;
  }
  float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  SDL_WindowFlags window_flags =
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  SDL_Window *window = SDL_CreateWindow("UNFV3", (int)(1080 * main_scale),
                                        (int)(800 * main_scale), window_flags);
  SDL_Log("Window Driver: %s\n", SDL_GetCurrentVideoDriver());
  if (window == nullptr) {
    printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(window);
  SDL_GPUDevice *gpu_device = SDL_CreateGPUDevice(
      SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
          SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB,
      true, nullptr);
  if (gpu_device == nullptr) {
    printf("Error: SDL_CreateGPUDevice(): %s\n", SDL_GetError());
    return 1;
  }
  printf("GPU driver: %s\n", SDL_GetGPUDeviceDriver(gpu_device));
  if (!SDL_ClaimWindowForGPUDevice(gpu_device, window)) {
    printf("Error: SDL_ClaimWindowForGPUDevice(): %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetGPUSwapchainParameters(gpu_device, window,
                                SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                SDL_GPU_PRESENTMODE_VSYNC);

  // ImGUI setup
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();
  style.ScaleAllSizes(main_scale);
  style.FontScaleDpi = main_scale;
  ImGui_ImplSDL3_InitForSDLGPU(window);
  ImGui_ImplSDLGPU3_InitInfo init_info = {};
  init_info.Device = gpu_device;
  init_info.ColorTargetFormat =
      SDL_GetGPUSwapchainTextureFormat(gpu_device, window);
  init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
  ImGui_ImplSDLGPU3_Init(&init_info);
  style.FontSizeBase = 20.0f;
  ImFont *font = io.Fonts->AddFontFromFileTTF("./Data/Quantico-Regular.ttf");
  IM_ASSERT(font != nullptr);
  ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);
  bool done = false;

  Database db = Database();
  std::mutex mutex;
  std::deque<Session> sessions;

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
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();

    ImGui::NewFrame();
    ImGui::BeginMainMenuBar();

    if (ImGui::BeginMenu("Tools")) {
      if (ImGui::MenuItem("Load Roll")) {
        auto *data = new FolderDialogData{&db, gpu_device, &sessions, &mutex};

        SDL_ShowOpenFolderDialog(load_roll_callback, data, window, ".", false);
      };
      if (ImGui::MenuItem("Load Mess List")) {
        SDL_ShowOpenFileDialog(mess_list_callback, &db, window, csv_filters, 1,
                               ".", false);
      };
      ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();

    std::lock_guard lock(mutex);

    if (!sessions.empty()) {
      ImGuiViewport *viewport = ImGui::GetMainViewport();
      float menu_bar_height = ImGui::GetFrameHeight();

      // Position below menu bar
      ImGui::SetNextWindowPos(
          ImVec2(viewport->Pos.x, viewport->Pos.y + menu_bar_height));

      // Fill remaining space
      ImGui::SetNextWindowSize(
          ImVec2(viewport->Size.x, viewport->Size.y - menu_bar_height));

      ImGuiWindowFlags flags =
          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

      ImGui::Begin("SessionTabsHost", nullptr, flags);

      // ONE tab bar
      if (ImGui::BeginTabBar("SessionsTabBar",
                             ImGuiTabBarFlags_Reorderable |
                                 ImGuiTabBarFlags_AutoSelectNewTabs)) {
        for (auto &session : sessions) {
          // Each session = one tab
          if (ImGui::BeginTabItem(
                  get_folder_name(session.manager.imageFolder).c_str())) {

            // Remember that the image in the ImageManager is Null before you do
            // this
            if (!session.manager.current_image)
              session.manager.loadImage();

            const float available_width = ImGui::GetContentRegionAvail().x;
            const float default_left_width = available_width * 0.38f;
            ImGui::BeginChild("LeftPanel", ImVec2(default_left_width, 0.0f),
                              ImGuiChildFlags_ResizeX);
            session.render_searcher();
            session.render_billed();
            ImGui::EndChild();

            ImGui::SameLine();
            session.manager.drawManager(&io);

            ImGui::EndTabItem();
          }
        }

        ImGui::EndTabBar();
      }

      ImGui::End();
    }

    if (db.show_loaded_csv)
      db.render_loaded_csv();
    ImGui::Render();

    ImDrawData *draw_data = ImGui::GetDrawData();
    const bool is_minimized =
        (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);

    SDL_GPUCommandBuffer *command_buffer =
        SDL_AcquireGPUCommandBuffer(gpu_device);

    SDL_GPUTexture *swapchain_texture;
    SDL_WaitAndAcquireGPUSwapchainTexture(
        command_buffer, window, &swapchain_texture, nullptr,
        nullptr); // Acquire a swapchain texture

    if (swapchain_texture != nullptr && !is_minimized) {
      ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);

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
      ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, render_pass);

      SDL_EndGPURenderPass(render_pass);
    }
    SDL_SubmitGPUCommandBuffer(command_buffer);
  }

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
