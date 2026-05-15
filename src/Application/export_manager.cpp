#include "include/export_manager.h"
#include "include/IconsFontAwesome6.h"
#include "include/random_civ_6_quote.h"
#include <filesystem>
#include <utility>

#define STB_TRUETYPE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "include/stb_image.h"
#include "include/stb_image_write.h"
#include "include/stb_truetype.h"

#include <SDL3/SDL.h>
#include <fstream>
#include <imgui.h>
#include <iostream>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

void SDLCALL prepare_export_queue(void *userdata, const char *const *folderlist,
                                  int) {
#ifdef TRACY_ENABLE
  ZoneScopedN("prepare_export_queue");
#endif

  auto *export_manager = static_cast<ExportManager *>(userdata);
  export_manager->pending.clear();
  export_manager->export_font_data.clear();
  const std::string roll = export_manager->path.filename().string();
  export_manager->export_output_directory =
      std::filesystem::path(*folderlist) / roll;

  size_t total_items = 0;
  for (const auto &[image_path, entries] : export_manager->bill) {
    for (const auto &[student_id, entry] : entries.entries) {
      (void)student_id;
      total_items += std::max(entry.count, 0);
    }
  }
  export_manager->pending.reserve(total_items);

  for (const auto &[image_path, entries] : export_manager->bill) {
    for (const auto &[student_id, entry] : entries.entries) {
      if (entry.count < 1) {
        continue;
      }

      const ExportInfo info =
          export_manager->database.get_export_information_from_id(student_id);
      const std::string watermark = info.bhawan + " " + info.roomno;
      for (int copy_index = 1; copy_index <= entry.count; ++copy_index) {
        const std::string filename = roll + "_" + image_path.stem().string() +
                                     "_" + info.bhawan + "_" + info.roomno +
                                     "_" + student_id + "_" +
                                     std::to_string(copy_index) + ".jpg";
        export_manager->pending.push_back(
            {image_path, export_manager->export_output_directory / filename,
             watermark, image_path.filename().string()});
      }
    }
  }

  export_manager->export_total =
      static_cast<int>(export_manager->pending.size());
  export_manager->export_progress = 0;
  export_manager->export_completed = false;
  export_manager->export_active_items.clear();

  std::ostringstream status;
  status << "Ready: " << export_manager->pending.size() << " image";
  if (export_manager->pending.size() != 1) {
    status << 's';
  }
  export_manager->export_status_message = status.str();
}

void ExportManager::process_pending_image(const PendingExport &image,
                                          size_t worker_index) {
#ifdef TRACY_ENABLE
  ZoneScopedN("ExportManager::process_pending_image");
#endif
  {
    std::lock_guard<std::mutex> lock(export_status_mutex);
    if (worker_index < export_active_items.size()) {
      export_active_items[worker_index] = image.label;
    }
  }

  int width = 0;
  int height = 0;
  int channels = 0;
  unsigned char *pixels =
      stbi_load(image.source.string().c_str(), &width, &height, &channels, 3);
  if (pixels == nullptr) {
    export_progress.fetch_add(1);
    std::lock_guard<std::mutex> lock(export_status_mutex);
    if (worker_index < export_active_items.size()) {
      export_active_items[worker_index] = "Skipped unreadable image";
    }
    std::cerr << "Failed to read export source image: " << image.source.string()
              << std::endl;
    return;
  }

  if (export_apply_watermark && !export_font_data.empty()) {
    stbtt_fontinfo font;
    stbtt_InitFont(&font, export_font_data.data(), 0);

    const float scale = stbtt_ScaleForPixelHeight(&font, height * 0.04f);
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);

    int text_width = 0;
    for (const char *c = image.watermark.c_str(); *c != '\0'; ++c) {
      int advance = 0;
      int lsb = 0;
      stbtt_GetCodepointHMetrics(&font, *c, &advance, &lsb);
      text_width += static_cast<int>(advance * scale);
    }

    const int text_height = static_cast<int>((ascent - descent) * scale);
    const int origin_x = width - text_width - 12;
    const int origin_y = height - text_height - 12;
    const int baseline = static_cast<int>(ascent * scale);

    struct Pass {
      int ox;
      int oy;
      unsigned char r;
      unsigned char g;
      unsigned char b;
    };

    for (const Pass pass : {Pass{2, 2, 0, 0, 0}, Pass{0, 0, 255, 255, 0}}) {
      int cursor_x = origin_x + pass.ox;
      const int cursor_y = origin_y + pass.oy;
      for (const char *c = image.watermark.c_str(); *c != '\0'; ++c) {
        int advance = 0;
        int lsb = 0;
        stbtt_GetCodepointHMetrics(&font, *c, &advance, &lsb);

        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBox(&font, *c, scale, scale, &x0, &y0, &x1,
                                    &y1);
        int glyph_w = x1 - x0;
        int glyph_h = y1 - y0;
        if (glyph_w > 0 && glyph_h > 0) {
          int gx0 = 0;
          int gy0 = 0;
          unsigned char *bitmap = stbtt_GetCodepointBitmap(
              &font, scale, scale, *c, &glyph_w, &glyph_h, &gx0, &gy0);
          for (int gy = 0; gy < glyph_h; ++gy) {
            for (int gx = 0; gx < glyph_w; ++gx) {
              const int px = cursor_x + gx0 + gx;
              const int py = cursor_y + baseline + gy0 + gy;
              if (px < 0 || px >= width || py < 0 || py >= height) {
                continue;
              }
              if (bitmap[gy * glyph_w + gx] > 128) {
                unsigned char *dst = pixels + (py * width + px) * 3;
                dst[0] = pass.r;
                dst[1] = pass.g;
                dst[2] = pass.b;
              }
            }
          }
          stbtt_FreeBitmap(bitmap, nullptr);
        }
        cursor_x += static_cast<int>(advance * scale);
      }
    }
  }

  stbi_write_jpg(image.destination.string().c_str(), width, height, 3, pixels,
                 95);
  stbi_image_free(pixels);
  export_progress.fetch_add(1);

  std::lock_guard<std::mutex> lock(export_status_mutex);
  if (worker_index < export_active_items.size()) {
    export_active_items[worker_index] = "Idle";
  }
}

void ExportManager::export_images() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::export_images");
#endif
  if (pending.empty()) {
    return;
  }

  const unsigned int hardware_threads = std::thread::hardware_concurrency();
  const size_t worker_count = std::max<size_t>(
      1, std::min(pending.size(), static_cast<size_t>(hardware_threads == 0
                                                          ? 4
                                                          : hardware_threads)));

  {
    std::lock_guard<std::mutex> lock(export_status_mutex);
    export_active_items.assign(worker_count, "Waiting to start");
  }

  std::atomic<size_t> next_index{0};
  std::vector<std::thread> workers;
  workers.reserve(worker_count);

  for (size_t worker_index = 0; worker_index < worker_count; ++worker_index) {
    workers.emplace_back([this, worker_index, &next_index]() {
      while (true) {
        const size_t item_index = next_index.fetch_add(1);
        if (item_index >= pending.size()) {
          std::lock_guard<std::mutex> lock(export_status_mutex);
          if (worker_index < export_active_items.size()) {
            export_active_items[worker_index] = "Idle";
          }
          break;
        }
        process_pending_image(pending[item_index], worker_index);
      }
    });
  }

  for (auto &worker : workers) {
    worker.join();
  }
}

void ExportManager::start_export() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::start_export");
#endif
  if (exporting || pending.empty()) {
    return;
  }
  if (export_worker.joinable()) {
    export_worker.join();
  }

  export_font_data.clear();
  if (export_apply_watermark) {
    std::ifstream font_file("./Data/Quantico-Regular.ttf", std::ios::binary);
    export_font_data.assign(std::istreambuf_iterator<char>(font_file), {});
    if (export_font_data.empty()) {
      export_status_message = "Font file missing: ./Data/Quantico-Regular.ttf";
      std::cerr << export_status_message;
      return;
    }
  }

  std::filesystem::create_directories(export_output_directory);
  export_progress = 0;
  export_completed = false;
  exporting = true;

  {
    std::lock_guard<std::mutex> lock(export_status_mutex);
    export_active_items.assign(1, "Waiting to start");
  }

  std::ostringstream status;
  status << "Writing to " << export_output_directory;
  export_status_message = status.str();
  export_worker = std::thread([this]() { export_images(); });
}

void ExportManager::finish_export_if_ready() {
  if (!exporting || export_progress.load() < export_total) {
    return;
  }

  exporting = false;
  export_completed = true;
  if (export_worker.joinable()) {
    export_worker.join();
  }

  std::lock_guard<std::mutex> lock(export_status_mutex);
  export_active_items.clear();

  std::ostringstream status;
  status << "Finished: " << export_total << " image";
  if (export_total != 1) {
    status << 's';
  }
  status << " written to " << export_output_directory;
  export_status_message = status.str();
}

void ExportManager::render_export_modal(SDL_Window *window) {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::draw_export_modal");
#endif
  finish_export_if_ready();

  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
  ImGui::OpenPopup(ICON_FA_FILE_EXPORT "  Export Roll");

  if (!ImGui::BeginPopupModal(ICON_FA_FILE_EXPORT "  Export Roll", nullptr,
                              ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }

  ImGui::PushTextWrapPos(0.0f); // wrap at window edge
  ImGui::TextColored(ImVec4(0.93f, 0.73f, 0.24f, 1.0f), quote.c_str());
  ImGui::PopTextWrapPos();

  ImGui::Separator();

  ImGui::Text("Roll");
  ImGui::SameLine(150.0f);
  ImGui::TextUnformatted(path.filename().string().c_str());

  ImGui::Text("Queued images");
  ImGui::SameLine(150.0f);
  ImGui::Text("%d", export_total);

  ImGui::Text("Destination");
  ImGui::SameLine(150.0f);
  ImGui::SetNextItemWidth(340.0f);
  const std::string folder_label =
      export_output_directory.empty()
          ? ICON_FA_FOLDER_OPEN "  Browse"
          : ICON_FA_FOLDER_CLOSED "  " +
                export_output_directory.parent_path().string();

  if (ImGui::SmallButton(folder_label.c_str()) && !exporting) {
    SDL_ShowOpenFolderDialog(prepare_export_queue, this, window, ".", false);
  }

  ImGui::Text("Options");
  ImGui::SameLine(150.0f);
  if (ImGui::Checkbox("Stamp watermark", &export_apply_watermark) &&
      !exporting) {
    export_status_message = export_apply_watermark
                                ? "Watermark stamping enabled"
                                : "Watermark stamping disabled";
  }
  ImGui::SameLine();

  ImGui::Spacing();
  ImGui::TextUnformatted(export_status_message.c_str());

  const float progress = export_total > 0
                             ? static_cast<float>(export_progress.load()) /
                                   static_cast<float>(export_total)
                             : 0.0f;
  const std::string progress_text = std::to_string(export_progress.load()) +
                                    " / " + std::to_string(export_total);
  ImGui::ProgressBar(exporting ? progress : (export_completed ? 1.0f : 0.0f),
                     ImVec2(520, 0), progress_text.c_str());

  std::vector<std::string> active_items;
  {
    std::lock_guard<std::mutex> lock(export_status_mutex);
    active_items = export_active_items;
  }

  ImGui::Spacing();
  ImGui::Text("Current image");
  ImGui::BeginChild("ExportActiveWork", ImVec2(520, 120), true);
  if (active_items.empty()) {
    ImGui::TextDisabled(exporting ? "Starting export..."
                                  : "Nothing is running right now.");
  } else {
    for (const auto &item : active_items) {
      ImGui::BulletText("%s", item.c_str());
    }
  }
  ImGui::EndChild();

  ImGui::Spacing();
  const bool can_start =
      !exporting && export_total > 0 && !export_output_directory.empty();
  if (!can_start) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button(export_completed ? "Run Again" : "Start Export",
                    ImVec2(150, 0))) {
    start_export();
  }
  if (!can_start) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  if (ImGui::Button(exporting ? "Close" : "Done", ImVec2(120, 0))) {
    draw_exporting = false;
    if (!exporting) {
      ImGui::CloseCurrentPopup();
    }
  }

  ImGui::EndPopup();
}

void ExportManager::autosave() {
#ifdef TRACY_ENABLE
  ZoneScopedN("ExportManager::autosave");
#endif
  const std::filesystem::path filepath = path / "save.json";
  nlohmann::json serialised = nlohmann::json::object();

  auto clean_bill = bill;
  for (auto &[image_path, file_data] : clean_bill) {
    std::erase_if(file_data.entries,
                  [](const auto &pair) { return pair.second.count < 1; });
    if (file_data.entries.empty())
      continue;

    std::string filename = image_path.filename().string();
    serialised[filename] = file_data;
  }

  for (const auto &[image_path, entry_map] : clean_bill) {
    std::string filename = image_path.filename().string();
    serialised[filename] = entry_map;
  }

  std::ofstream file(filepath, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file: " + filepath.string());
  }

  file << serialised.dump(4);
  if (!file.good()) {
    throw std::runtime_error("Error writing to file: " + filepath.string());
  }
  bill = std::move(clean_bill);
  request_autosave = false;
}

void ExportManager::same_as(const std::filesystem::path &source_image,
                            const std::filesystem::path &destination_image) {
#ifdef TRACY_ENABLE
  ZoneScopedN("ExportManager::append_bill_from_image");
#endif
  const auto image_path = destination_image;

  const auto source_it = bill.find(source_image);
  if (source_it == bill.end()) {
    return;
  }

  auto &current_bill = bill[image_path];
  for (const auto &[student_id, source_entry] : source_it->second.entries) {
    BillEntry &entry = current_bill.entries[student_id];
    entry.name = source_entry.name;
    entry.count += source_entry.count;
  }
  autosave();
}

void ExportManager::load_existing_bill() {
#ifdef TRACY_ENABLE
  ZoneScopedN("ExportManager::load_existing_bill");
#endif
  const std::filesystem::path filepath = path / "save.json";
  if (!std::filesystem::exists(filepath)) {
    return;
  }

  std::ifstream file(filepath);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file: " + filepath.string());
  }

  nlohmann::json json_data;
  file >> json_data;
  bill.clear();

  // Manually deserialize to reconstruct the full path
  for (auto it = json_data.begin(); it != json_data.end(); ++it) {
    // Reconstruct the full path by appending it to the directory 'path'
    std::filesystem::path full_path = path / it.key();
    bill[full_path] = it.value().get<BillFile>();
  }
}

void ExportManager::open_export_modal() {
  draw_exporting = true;
  quote = get_random_civ6_quote();
}

void ExportManager::increment_for_id(const std::string &id,
                                     const std::string name,
                                     const std::filesystem::path image) {
  auto &entries = bill[image].entries;
  BillEntry &entry = entries[id];
  entry.name = name;
  entry.count += 1;
  autosave();
}
