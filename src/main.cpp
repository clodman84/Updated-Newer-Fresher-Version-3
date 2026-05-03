#include "Application/operations/gimp_levels.h"
#include "Application/operations/my_colour_enhance.h"
#include "include/google_drive_browser.h"
#include "include/session.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

#include <SDL3/SDL.h>
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
static const SDL_DialogFileFilter csv_filters[] = {{"CSV files", "csv"}};
void log_dialog_error(const char *action) {
  std::cerr << action << ": " << SDL_GetError() << std::endl;
}
} // namespace

class Application {
public:
  Application(int argc, char *argv[]) : argc_(argc), argv_(argv) {}
  ~Application() { cleanup(); }

  int run() {
    if (!init())
      return 1;

    bool done = false;
    while (!done) {
#ifdef TRACY_ENABLE
      FrameMark;
#endif
      process_events(done);
      process_pending_sessions();

      if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) {
        SDL_Delay(10);
        continue;
      }

      render_frame();
    }
    return 0;
  }

private:
  int argc_;
  char **argv_;

  SDL_Window *window_ = nullptr;
  SDL_GPUDevice *gpu_device_ = nullptr;

  Database db_;
  std::deque<std::unique_ptr<Session>> sessions_;

  std::mutex pending_mutex_;
  std::vector<std::filesystem::path> pending_session_paths_;

  bool show_drive_browser_ = false;
  std::unique_ptr<GoogleDriveBrowser> drive_browser_;

  bool init() {
    setup_environment();

    gegl_init(nullptr, nullptr);
    gimp_levels_op_register();
    colour_enhance_op_register();
    g_object_set(gegl_config(), "mipmap-rendering", TRUE, nullptr);
    srand(static_cast<unsigned int>(time(nullptr)));

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
      std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
      return false;
    }

    const float main_scale =
        SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    const SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE |
                                         SDL_WINDOW_HIDDEN |
                                         SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window_ = SDL_CreateWindow("UNFV3", (int)(1080 * main_scale),
                               (int)(800 * main_scale), window_flags);

    if (!window_) {
      std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
      return false;
    }

    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED,
                          SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window_);

    gpu_device_ = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
            SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB,
        true, nullptr);

    if (!gpu_device_ || !SDL_ClaimWindowForGPUDevice(gpu_device_, window_)) {
      std::cerr << "GPU Device creation/claim failed: " << SDL_GetError()
                << std::endl;
      return false;
    }

    SDL_SetGPUSwapchainParameters(gpu_device_, window_,
                                  SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                  SDL_GPU_PRESENTMODE_VSYNC);

    if (!init_imgui(main_scale))
      return false;

    return true;
  }

  void setup_environment() {
    std::filesystem::path exePath = std::filesystem::weakly_canonical(argv_[0]);
    std::filesystem::current_path(exePath.parent_path());

    const char *base_path = SDL_GetBasePath();
    if (base_path) {
      std::filesystem::path root_dir(base_path);
      std::filesystem::path bundled_gegl = root_dir / "lib" / "gegl-0.4";
      std::filesystem::path bundled_babl = root_dir / "lib" / "babl-0.1";

      if (std::filesystem::exists(bundled_gegl)) {
        SDL_Log("Bundle detected! Redirecting GEGL/BABL paths");
#ifdef _WIN32
        _putenv_s("GEGL_PATH", bundled_gegl.generic_string().c_str());
        _putenv_s("BABL_PATH", bundled_babl.generic_string().c_str());
#else
        setenv("GEGL_PATH", bundled_gegl.string().c_str(), 1);
        setenv("BABL_PATH", bundled_babl.string().c_str(), 1);
#endif
      }
    }
  }

  bool init_imgui(float main_scale) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    ImGui::StyleColorsDark();

    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;
    style.FontSizeBase = 20.0f;

    if (!ImGui_ImplSDL3_InitForSDLGPU(window_))
      return false;

    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.Device = gpu_device_;
    init_info.ColorTargetFormat =
        SDL_GetGPUSwapchainTextureFormat(gpu_device_, window_);
    init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;

    if (!ImGui_ImplSDLGPU3_Init(&init_info))
      return false;

    if (!io.Fonts->AddFontFromFileTTF("./Data/Quantico-Regular.ttf")) {
      std::cerr
          << "Warning: Failed to load UI font: ./Data/Quantico-Regular.ttf\n";
    }
    return true;
  }

  void cleanup() {
    gegl_exit();
    sessions_.clear();
    if (gpu_device_) {
      SDL_WaitForGPUIdle(gpu_device_);
      ImGui_ImplSDL3_Shutdown();
      ImGui_ImplSDLGPU3_Shutdown();
      ImGui::DestroyContext();
      if (window_)
        SDL_ReleaseWindowFromGPUDevice(gpu_device_, window_);
      SDL_DestroyGPUDevice(gpu_device_);
    }
    if (window_)
      SDL_DestroyWindow(window_);
    SDL_Quit();
  }

  void process_events(bool &done) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) {
        done = true;
      }
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
          event.window.windowID == SDL_GetWindowID(window_)) {
        done = true;
      }
    }
  }

  void process_pending_sessions() {
#ifdef TRACY_ENABLE
    ZoneScopedN("process_pending_sessions");
#endif
    std::vector<std::filesystem::path> pending_paths;
    {
      std::lock_guard lock(pending_mutex_);
      pending_paths.swap(pending_session_paths_);
    }

    for (const auto &session_path : pending_paths) {
      try {
        sessions_.push_back(
            std::make_unique<Session>(session_path, gpu_device_));
        sessions_.back()->image_manager.load_folder(gpu_device_);
        sessions_.back()->image_manager.load_image();
      } catch (const std::exception &error) {
        std::cerr << "Failed to create session: " << error.what() << std::endl;
      }
    }
  }

  void render_frame() {
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    render_menu_bar();
    render_sessions();

    if (db_.show_loaded_csv) {
      db_.render_loaded_csv();
    }

    if (show_drive_browser_ && drive_browser_) {
      drive_browser_->render_window("Google Drive Browser");
    }

    ImGui::Render();
    submit_gpu_commands();
  }

  void render_menu_bar() {
    ImGui::BeginMainMenuBar();
    if (ImGui::BeginMenu("Tools")) {
      if (ImGui::MenuItem("Load Roll")) {
        SDL_ShowOpenFolderDialog(load_roll_callback, this, window_, ".", false);
      }
      if (ImGui::MenuItem("Load Mess List")) {
        SDL_ShowOpenFileDialog(mess_list_callback, this, window_, csv_filters,
                               1, ".", false);
      }
      if (ImGui::BeginMenu("Export Roll")) {
        for (auto &session_ptr : sessions_) {
          if (ImGui::MenuItem(session_ptr->folder_path.string().c_str())) {
            session_ptr->export_manager.open_export_modal();
          }
        }
        ImGui::EndMenu();
      }

      if (ImGui::MenuItem("Google Drive", nullptr, show_drive_browser_)) {
        show_drive_browser_ = !show_drive_browser_;

        if (show_drive_browser_ && !drive_browser_) {
          try {
            // TODO: Swap out the file loading with something that is loaded
            // from the database
            drive_browser_ = std::make_unique<GoogleDriveBrowser>(window_);
          } catch (const std::exception &e) {
            std::cerr << "Failed to initialize Drive Browser: " << e.what()
                      << std::endl;
            show_drive_browser_ = false;
          }
        }
      }

      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }

  void render_sessions() {
    if (sessions_.empty())
      return;
#ifdef TRACY_ENABLE
    ZoneScopedN("render_sessions");
#endif
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float menu_bar_height = ImGui::GetFrameHeight();

    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x, viewport->Pos.y + menu_bar_height));
    ImGui::SetNextWindowSize(
        ImVec2(viewport->Size.x, viewport->Size.y - menu_bar_height));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

    ImGui::Begin("SessionTabsHost", nullptr, flags);
    if (ImGui::BeginTabBar("SessionsTabBar",
                           ImGuiTabBarFlags_Reorderable |
                               ImGuiTabBarFlags_AutoSelectNewTabs)) {
      for (auto it = sessions_.begin(); it != sessions_.end();) {
        Session &session = **it;
        if (session.export_manager.draw_exporting) {
          session.export_manager.render_export_modal(window_);
        }

        bool open = true;
        if (ImGui::BeginTabItem(get_folder_name(session.folder_path).c_str(),
                                &open)) {
          session.handle_keyboard_nav();
          const float left_width = ImGui::GetContentRegionAvail().x * 0.38f;

          ImGui::BeginChild("LeftPanel", ImVec2(left_width, 0.0f),
                            ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
          session.render_searcher();
          session.render_billed();
          ImGui::EndChild();

          ImGui::SameLine();
          session.render_image_panel();
          ImGui::EndTabItem();
        }

        if (!open)
          it = sessions_.erase(it);
        else
          ++it;
      }
      ImGui::EndTabBar();
    }
    ImGui::End();
  }

  void submit_gpu_commands() {
    ImDrawData *draw_data = ImGui::GetDrawData();
    if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
      return;

    SDL_GPUCommandBuffer *cmd_buffer = SDL_AcquireGPUCommandBuffer(gpu_device_);
    SDL_GPUTexture *swapchain_tex = nullptr;
    SDL_WaitAndAcquireGPUSwapchainTexture(cmd_buffer, window_, &swapchain_tex,
                                          nullptr, nullptr);

    if (swapchain_tex) {
      ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, cmd_buffer);

      SDL_GPUColorTargetInfo target_info = {};
      target_info.texture = swapchain_tex;
      target_info.clear_color = SDL_FColor{0.f, 0.f, 0.f, 1.0f};
      target_info.load_op = SDL_GPU_LOADOP_CLEAR;
      target_info.store_op = SDL_GPU_STOREOP_STORE;

      SDL_GPURenderPass *render_pass =
          SDL_BeginGPURenderPass(cmd_buffer, &target_info, 1, nullptr);
      ImGui_ImplSDLGPU3_RenderDrawData(draw_data, cmd_buffer, render_pass);
      SDL_EndGPURenderPass(render_pass);
    }
    SDL_SubmitGPUCommandBuffer(cmd_buffer);
  }
  static void SDLCALL load_roll_callback(void *userdata,
                                         const char *const *filelist,
                                         int filter) {
#ifdef TRACY_ENABLE
    ZoneScopedN("load_roll_callback");
#endif
    auto *app = static_cast<Application *>(userdata);
    if (!app || !filelist) {
      log_dialog_error("Folder dialog failed");
      return;
    }
    if (!*filelist)
      return; // Canceled

    std::lock_guard lock(app->pending_mutex_);
    while (*filelist) {
      app->pending_session_paths_.emplace_back(*filelist);
      ++filelist;
    }
  }

  static void SDLCALL mess_list_callback(void *userdata,
                                         const char *const *filelist,
                                         int filter) {
#ifdef TRACY_ENABLE
    ZoneScopedN("mess_list_callback");
#endif
    auto *app = static_cast<Application *>(userdata);
    if (!app || !filelist) {
      log_dialog_error("CSV dialog failed");
      return;
    }
    if (!*filelist)
      return; // Canceled

    while (*filelist) {
      try {
        app->db_.read_csv(*filelist);
        app->db_.show_loaded_csv = true;
      } catch (const std::exception &error) {
        std::cerr << "Failed to load CSV: " << error.what() << std::endl;
      }
      ++filelist;
    }
  }
};

int main(int argc, char *argv[]) {
  prepare_database();
  Application app(argc, argv);
  return app.run();
}
