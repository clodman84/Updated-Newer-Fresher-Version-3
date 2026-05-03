#include "Application/include/drive_browser_ui.h"
#include "Application/operations/gimp_levels.h"
#include "Application/operations/my_colour_enhance.h"
#include "include/session.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

#include <SDL3/SDL.h>
#include <cstdio>
#include <gegl-init.h>
#include <gegl.h>

#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

namespace {

std::string get_folder_name(const std::filesystem::path &full_path) {
  return full_path.filename().string();
}

struct AppState {
  Database *database = nullptr;
  SDL_GPUDevice *device = nullptr;
  bool show_drive = false;
  std::mutex pending_mutex;
  std::vector<std::filesystem::path> pending_session_paths;
};

static const SDL_DialogFileFilter csv_filters[] = {{"CSV files", "csv"}};

void log_dialog_error(const char *action) {
  std::cerr << action << ": " << SDL_GetError() << std::endl;
}

static void SDLCALL mess_list_callback(void *userdata,
                                       const char *const *filelist,
                                       int filter) {
#ifdef TRACY_ENABLE
  ZoneScopedN("mess_list_callback");
#endif
  auto *database = static_cast<Database *>(userdata);
  if (database == nullptr) {
    std::cerr << "CSV dialog callback received null database." << std::endl;
    return;
  }

  if (filelist == nullptr) {
    log_dialog_error("CSV dialog failed");
    return;
  }
  if (*filelist == nullptr) {
    std::cerr << "CSV dialog canceled by user." << std::endl;
    return;
  }

  while (*filelist != nullptr) {
    const std::filesystem::path filename(*filelist);
    try {
      database->read_csv(filename.string());
      database->show_loaded_csv = true;
    } catch (const std::exception &error) {
      std::cerr << "Failed to load CSV '" << filename.string()
                << "': " << error.what() << std::endl;
    }
    ++filelist;
  }

  if (filter >= 0 && filter < SDL_arraysize(csv_filters)) {
    SDL_Log("CSV filter selected: %s (%s)", csv_filters[filter].pattern,
            csv_filters[filter].name);
  }
}

static void SDLCALL load_roll_callback(void *userdata,
                                       const char *const *filelist,
                                       int filter) {
  (void)filter;
#ifdef TRACY_ENABLE
  ZoneScopedN("load_roll_callback");
#endif
  auto *app_state = static_cast<AppState *>(userdata);
  if (app_state == nullptr) {
    std::cerr << "Load roll callback received null app state." << std::endl;
    return;
  }

  if (filelist == nullptr) {
    log_dialog_error("Folder dialog failed");
    return;
  }
  if (*filelist == nullptr) {
    std::cerr << "Folder dialog canceled by user." << std::endl;
    return;
  }

  std::lock_guard lock(app_state->pending_mutex);
  while (*filelist != nullptr) {
    app_state->pending_session_paths.emplace_back(*filelist);
    ++filelist;
  }
}

bool init_imgui(SDL_Window *window, SDL_GPUDevice *gpu_device,
                float main_scale) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();
  style.ScaleAllSizes(main_scale);
  style.FontScaleDpi = main_scale;
  style.FontSizeBase = 20.0f;

  if (!ImGui_ImplSDL3_InitForSDLGPU(window)) {
    std::cerr << "Failed to initialize ImGui SDL backend." << std::endl;
    return false;
  }

  ImGui_ImplSDLGPU3_InitInfo init_info = {};
  init_info.Device = gpu_device;
  init_info.ColorTargetFormat =
      SDL_GetGPUSwapchainTextureFormat(gpu_device, window);
  init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
  if (!ImGui_ImplSDLGPU3_Init(&init_info)) {
    std::cerr << "Failed to initialize ImGui SDL GPU backend." << std::endl;
    return false;
  }

  ImFont *font = io.Fonts->AddFontFromFileTTF("./Data/Quantico-Regular.ttf");
  if (font == nullptr) {
    std::cerr << "Failed to load UI font: ./Data/Quantico-Regular.ttf"
              << std::endl;
    return false;
  }
  return true;
}

void process_pending_sessions(AppState &app_state,
                              std::deque<std::unique_ptr<Session>> &sessions) {
#ifdef TRACY_ENABLE
  ZoneScopedN("process_pending_sessions");
#endif
  std::vector<std::filesystem::path> pending_paths;
  {
    std::lock_guard lock(app_state.pending_mutex);
    pending_paths.swap(app_state.pending_session_paths);
  }

  for (const auto &session_path : pending_paths) {
    try {
      sessions.push_back(
          std::make_unique<Session>(session_path, app_state.device));
      sessions.back()->image_manager.load_folder(app_state.device);
      sessions.back()->image_manager.load_image();
    } catch (const std::exception &error) {
      std::cerr << "Failed to create session for '" << session_path.string()
                << "': " << error.what() << std::endl;
    }
  }
}

void render_menu_bar(SDL_Window *window, Database &db, AppState &app_state,
                     std::deque<std::unique_ptr<Session>> &sessions) {
  ImGui::BeginMainMenuBar();
  if (ImGui::BeginMenu("Tools")) {
    if (ImGui::MenuItem("Load Roll")) {
      SDL_ShowOpenFolderDialog(load_roll_callback, &app_state, window, ".",
                               false);
    }
    if (ImGui::MenuItem("Load Mess List")) {
      SDL_ShowOpenFileDialog(mess_list_callback, &db, window, csv_filters, 1,
                             ".", false);
    }
    if (ImGui::BeginMenu("Export Roll")) {
      for (auto &session_ptr : sessions) {
        Session &session = *session_ptr;
        const std::string session_label = session.folder_path.string();
        if (ImGui::MenuItem(session_label.c_str())) {
          session.export_manager.open_export_modal();
        }
      }
      ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Drive Link")) {
      DriveBrowserUI::open();
      app_state.show_drive = true;
    }
    ImGui::EndMenu();
  }
  ImGui::EndMainMenuBar();
}

void render_sessions(std::deque<std::unique_ptr<Session>> &sessions,
                     SDL_Window *window) {
  if (sessions.empty()) {
    return;
  }
  auto io = ImGui::GetIO();
#ifdef TRACY_ENABLE
  ZoneScopedN("render_sessions");
#endif

  ImGuiViewport *viewport = ImGui::GetMainViewport();
  const float menu_bar_height = ImGui::GetFrameHeight();

  ImGui::SetNextWindowPos(
      ImVec2(viewport->Pos.x, viewport->Pos.y + menu_bar_height));
  ImGui::SetNextWindowSize(
      ImVec2(viewport->Size.x, viewport->Size.y - menu_bar_height));

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                           ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoCollapse;
  ImGui::Begin("SessionTabsHost", nullptr, flags);

  if (ImGui::BeginTabBar("SessionsTabBar",
                         ImGuiTabBarFlags_Reorderable |
                             ImGuiTabBarFlags_AutoSelectNewTabs)) {
    for (auto it = sessions.begin(); it != sessions.end();) {
      Session &session = **it;
      if (session.export_manager.draw_exporting) {
        session.export_manager.render_export_modal(window);
      }

      bool open = true;
      const std::string tab_name = get_folder_name(session.folder_path);
      if (ImGui::BeginTabItem(tab_name.c_str(), &open)) {
        session.handle_keyboard_nav();
        const float available_width = ImGui::GetContentRegionAvail().x;
        const float default_left_width = available_width * 0.38f;

        ImGui::BeginChild("LeftPanel", ImVec2(default_left_width, 0.0f),
                          ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
        session.render_searcher();
        session.render_billed();
        ImGui::EndChild();
        ImGui::SameLine();

        session.render_image_panel();
        ImGui::EndTabItem();
      }

      if (!open) {
        it = sessions.erase(it);
      } else {
        ++it;
      }
    }
    ImGui::EndTabBar();
  }

  ImGui::End();
}

} // namespace

int main(int argc, char *argv[]) {

  // This is a fucking nightmare
  std::filesystem::path exePath = std::filesystem::weakly_canonical(argv[0]);
  // Change the working directory to the folder containing the executable
  std::filesystem::current_path(exePath.parent_path());

  prepare_database();
  const char *base_path = SDL_GetBasePath();
  if (base_path) {
    std::filesystem::path root_dir(base_path);
    std::filesystem::path bundled_gegl = root_dir / "lib" / "gegl-0.4";
    std::filesystem::path bundled_babl = root_dir / "lib" / "babl-0.1";

    // ONLY apply if we are running from a bundle/installation
    if (std::filesystem::exists(bundled_gegl)) {
      SDL_Log("Bundle detected! Redirecting GEGL/BABL paths to %s and %s",
              bundled_gegl.generic_string().c_str(),
              bundled_babl.generic_string().c_str());
#ifdef _WIN32
      _putenv_s("GEGL_PATH", bundled_gegl.generic_string().c_str());
      _putenv_s("BABL_PATH", bundled_babl.generic_string().c_str());
#else
      setenv("GEGL_PATH", bundled_gegl.string().c_str(), 1);
      setenv("BABL_PATH", bundled_babl.string().c_str(), 1);
#endif
    } else {
      SDL_Log("No bundle found. Using system GEGL/BABL paths.");
    }
  }

  gegl_init(NULL, NULL);
  gimp_levels_op_register();
  colour_enhance_op_register();
  GeglConfig *config = gegl_config();
  g_object_set(config, "mipmap-rendering", TRUE, NULL);

  srand(time(NULL));

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
    return 1;
  }

  const float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  const SDL_WindowFlags window_flags =
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  SDL_Window *window = SDL_CreateWindow("UNFV3", (int)(1080 * main_scale),
                                        (int)(800 * main_scale), window_flags);
  if (window == nullptr) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
    SDL_Quit();
    return 1;
  }

  SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(window);

  SDL_GPUDevice *gpu_device = SDL_CreateGPUDevice(
      SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
          SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB,
      true, nullptr);
  if (gpu_device == nullptr) {
    std::cerr << "SDL_CreateGPUDevice failed: " << SDL_GetError() << std::endl;
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  if (!SDL_ClaimWindowForGPUDevice(gpu_device, window)) {
    std::cerr << "SDL_ClaimWindowForGPUDevice failed: " << SDL_GetError()
              << std::endl;
    SDL_DestroyGPUDevice(gpu_device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  SDL_SetGPUSwapchainParameters(gpu_device, window,
                                SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                SDL_GPU_PRESENTMODE_VSYNC);

  if (!init_imgui(window, gpu_device, main_scale)) {
    SDL_ReleaseWindowFromGPUDevice(gpu_device, window);
    SDL_DestroyGPUDevice(gpu_device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  ImGuiIO &io = ImGui::GetIO();
  const ImVec4 clear_color = ImVec4(0.f, 0.f, 0.f, 1.0f);
  bool done = false;

  Database db;
  AppState app_state{.database = &db, .device = gpu_device};
  std::deque<std::unique_ptr<Session>> sessions;

  while (!done) {
#ifdef TRACY_ENABLE
    FrameMark;
#endif
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) {
        done = true;
      }
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
          event.window.windowID == SDL_GetWindowID(window)) {
        done = true;
      }
    }

    process_pending_sessions(app_state, sessions);

    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
      SDL_Delay(10);
      continue;
    }

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    render_menu_bar(window, db, app_state, sessions);
    render_sessions(sessions, window);

    if (db.show_loaded_csv) {
      db.render_loaded_csv();
    }

    if (app_state.show_drive)
      DriveBrowserUI::draw();

    ImGui::Render();

    ImDrawData *draw_data = ImGui::GetDrawData();
    const bool is_minimized =
        draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f;

    SDL_GPUCommandBuffer *command_buffer =
        SDL_AcquireGPUCommandBuffer(gpu_device);
    SDL_GPUTexture *swapchain_texture = nullptr;
    SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, window,
                                          &swapchain_texture, nullptr, nullptr);

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

  gegl_exit();
  sessions.clear();
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
