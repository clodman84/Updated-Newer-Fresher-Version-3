#include "include/session.h"
#include <imgui.h>
#include <imgui_internal.h>

#include <SDL3/SDL_log.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

void Session::sync_search_selection_bounds() {
  if (search_results.empty()) {
    selected_search_index = 0;
    return;
  }
  selected_search_index =
      std::clamp(selected_search_index, 0, (int)search_results.size() - 1);
}

void Session::sync_billed_selection_bounds() {
  if (image_manager.current_image == nullptr) {
    return;
  }
  const int entry_count = export_manager.visible_billed_entry_count(
      image_manager.current_image->filename);
  if (entry_count == 0) {
    selected_billed_index = 0;
    return;
  }
  selected_billed_index = std::clamp(selected_billed_index, 0, entry_count - 1);
}

void Session::handle_search_keyboard_nav() {
  if (image_manager.current_image == nullptr) {
    return;
  }
  auto entries = export_manager.bill[image_manager.current_image->filename];
  if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !entries.empty()) {
    keyboard_nav_mode = KeyboardNavMode::Billed;
    focus_billed_on_next_frame = true;
    return;
  }

  if (search_results.empty()) {
    return;
  }

  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
    selected_search_index = std::max(0, selected_search_index - 1);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
    selected_search_index =
        std::min((int)search_results.size() - 1, selected_search_index + 1);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
    const auto &line = search_results[selected_search_index];
    if (image_manager.current_image != nullptr)
      export_manager.increment_for_id(line[0], line[1],
                                      image_manager.current_image->filename);
  }
}

void Session::handle_billed_keyboard_nav() {
  if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
    keyboard_nav_mode = KeyboardNavMode::Search;
    focus_search_on_next_frame = true;
    return;
  }

  auto entries = export_manager.bill[image_manager.current_image->filename];
  if (export_manager.visible_billed_entry_count(
          image_manager.current_image->filename) == 0) {
    return;
  }

  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
    selected_billed_index = std::max(0, selected_billed_index - 1);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
    selected_billed_index =
        std::min(export_manager.visible_billed_entry_count(
                     image_manager.current_image->filename) -
                     1,
                 selected_billed_index + 1);
  }

  if (!ImGui::IsKeyPressed(ImGuiKey_Enter)) {
    return;
  }

  int visible_index = 0;
  for (auto &[student_id, entry] : entries) {
    (void)student_id;
    if (entry.count < 1) {
      continue;
    }
    if (visible_index == selected_billed_index) {
      entry.count += 1;
      export_manager.autosave();
      return;
    }
    ++visible_index;
  }
}

void Session::handle_keyboard_nav() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::handle_keyboard_nav");
#endif
  if (!ImGui::GetIO().WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
      image_manager.load_previous();
      reset_view_to_image();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
      image_manager.load_next();
      reset_view_to_image();
    }
  }

  sync_search_selection_bounds();
  sync_billed_selection_bounds();

  if (keyboard_nav_mode == KeyboardNavMode::Search) {
    handle_search_keyboard_nav();
  } else {
    handle_billed_keyboard_nav();
  }
}
