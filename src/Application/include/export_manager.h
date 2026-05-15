#pragma once

#include <atomic>
#include <filesystem>
#include <include/database.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

struct BillEntry {
  std::string name;
  int count = 0;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE(BillEntry, name, count);
};

struct FileAttributes {
  bool bookmark = false;
  bool finalised = false;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE(FileAttributes, bookmark, finalised);
};

struct BillFile {
  FileAttributes attributes;
  std::map<std::string, BillEntry> entries;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE(BillFile, attributes, entries);
};

using BillMap = std::unordered_map<std::filesystem::path, BillFile>;
struct PendingExport {
  std::filesystem::path source;
  std::filesystem::path destination;
  std::string watermark;
  std::string label;
};

class ExportManager {
public:
  ExportManager(std::filesystem::path path) : path(path) {
    load_existing_bill();
  };
  ~ExportManager() = default;
  std::filesystem::path path;
  BillMap bill;
  bool request_autosave = false;

  void export_images();
  void open_export_modal();
  void render_export_modal(SDL_Window *window);
  void autosave();
  void render_billed_table();

  void start_export();
  void finish_export_if_ready();

  int visible_billed_entry_count(std::filesystem::path path) {
    auto entries = bill[path].entries;
    int count = 0;
    for (const auto &[id, entry] : entries) {
      (void)id;
      if (entry.count > 0) {
        ++count;
      }
    }
    return count;
  }

  std::vector<PendingExport> pending;
  std::vector<unsigned char> export_font_data;
  std::string export_status_message = "Ready to export";
  std::filesystem::path export_output_directory;
  int export_total = 0;
  bool exporting = false;
  bool export_completed = false;
  bool draw_exporting = false;
  std::atomic<int> export_progress{0};
  std::vector<std::string> export_active_items;
  Database database;
  void same_as(const std::filesystem::path &source_image,
               const std::filesystem::path &destination_image);
  void increment_for_id(const std::string &id, const std::string name,
                        const std::filesystem::path image);

private:
  std::mutex export_status_mutex;
  void process_pending_image(const PendingExport &image, size_t worker_index);
  void load_existing_bill();
  std::thread export_worker;
  std::string quote;
  bool export_apply_watermark = true;
};
