#include "include/dino_jumpy.h"
#include "include/imgui_custom.h"
#include <algorithm>
#include <imgui.h>

Game::Game() { dino = {{0, 0}, {0, 0}, {20, 20}}; }

void Game::move_dino() {

  if (ImGui::IsKeyDown(ImGuiKey_LeftShift))
    dino.movement_modifier = Dashed;
  else
    dino.movement_modifier = Regular;

  float x_modifier = 1;
  float y_modifier = 1;
  float g_modifier = 1;
  switch (dino.movement_modifier) {
  case Regular:
    break;
  case Dashed:
    x_modifier = 1.5;
    y_modifier = 1.5;
    break;
  };

  if (dino.action == GroundSlam) {
    g_modifier = 2;
  }

  if (ImGui::IsKeyPressed(ImGuiKey_LeftCtrl)) {
    if (dino.surface == Air) {
      dino.velocity.y = -dino.pos.y / 10;
      dino.action = GroundSlam;
    }
  }

  if (ImGui::IsKeyPressed(ImGuiKey_Space) &&
      !ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) {
    if (dino.surface == Ground) {
      dino.velocity.y = 20.0 * y_modifier;
      dino.surface = Air;
    } else {
      if (!(dino.action == DoubleJumped) &&
          !(dino.movement_modifier == Dashed)) {
        dino.velocity.y = 20.0 * y_modifier;
        dino.action = DoubleJumped;
      }
    }
  }

  if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow))
    dino.velocity.x = 10 * x_modifier;
  if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow))
    dino.velocity.x = -10 * x_modifier;

  if (dino.surface == Air)
    dino.velocity.y -= vars.acceleration.y * g_modifier;

  if (dino.surface == Ground) {
    if (dino.velocity.x > 0)
      dino.velocity.x = std::max(dino.velocity.x - vars.friction, 0.f);
    if (dino.velocity.x < 0)
      dino.velocity.x = std::min(dino.velocity.x + vars.friction, 0.f);
  }

  dino.pos.y = std::clamp(dino.pos.y + dino.velocity.y, 0.f, canvas_size.y);
  dino.pos.x = std::clamp(dino.pos.x + dino.velocity.x, 0.f,
                          canvas_size.x - dino.size.x);

  bool now_grounded = dino.pos.y <= 0;
  if (dino.surface == Air && now_grounded) {
    dino.surface = Ground;
    dino.action = None;
  }
}

void Game::render_frame() {
  if (vars.state == Running) {
    const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    canvas_size = ImGui::GetContentRegionAvail();
    move_dino();

    for (auto obstacle : obstacles) {
      obstacle.pos.x -= vars.game_speed;
      if (check_collision(obstacle))
        vars.state = Over;
    }

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImVec2 p_min = {dino.pos.x, canvas_size.y - dino.pos.y - dino.size.y};
    ImVec2 p_max = {p_min.x + dino.size.x, p_min.y + dino.size.y};

    ImColor colour = ImGui::GetColorU32({0, 255, 0, 255});

    if (dino.action == GroundSlam)
      colour = ImGui::GetColorU32({255, 0, 0, 255});
    if (dino.action == DoubleJumped || dino.movement_modifier == Dashed)
      colour = ImGui::GetColorU32({240, 128, 0, 255});

    draw_list->AddRectFilled(canvas_pos + p_min, canvas_pos + p_max, colour);
  }
}

bool Game::check_collision(Obstacle obstacle) { return false; }

void Game::make_obstacle() {}
