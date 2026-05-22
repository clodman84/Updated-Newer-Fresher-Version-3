#pragma once
#include <cmath>
#include <imgui.h>

inline void draw_spinner() {
  ImVec2 pos = ImGui::GetCursorScreenPos();
  float radius = 10.0f;
  float thickness = 2.5f;
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  float time = (float)ImGui::GetTime();
  ImDrawListFlags old_flags = draw_list->Flags;
  draw_list->Flags |=
      ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines;

  float pulse = (sinf(time * 4.0f) + 1.0f) * 0.5f;
  float arc_span = (0.4f + pulse * 1.4f) * M_PI;
  float angle_offset = time * 3.5f;
  int num_segments = 32;

  ImVec2 center = ImVec2(pos.x + radius, pos.y + 4 + radius);

  draw_list->PathClear();
  for (int i = 0; i <= num_segments; i++) {
    float angle = angle_offset + ((float)i / num_segments) * arc_span;
    draw_list->PathLineTo(ImVec2(center.x + cosf(angle) * radius,
                                 center.y + sinf(angle) * radius));
  }
  ImU32 arc_col = IM_COL32(120 + (int)(pulse * 135), 180, 255, 255);
  draw_list->PathStroke(arc_col, false, thickness);
  ImGui::Dummy(ImVec2(radius * 2.0f, radius * 2.0f + 4.0f));
}
