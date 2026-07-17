#pragma once
#include <imgui.h>
#include <vector>

enum GameState { Running, Over };

struct GameVars {
  int points = 0;
  float game_speed = 1.0f;
  float friction = 0.8;
  ImVec2 acceleration = {0, 0.7};
  GameState state = Running;
};

enum DinoMovementModifier { Regular, Dashed };
enum DinoAction { None, Dodge, GroundSlam, DoubleJumped };
enum DinoSurface { Ground, Air };
enum DinoVulnerability { Invincible };

struct Dino {
  ImVec2 pos;
  ImVec2 velocity;
  ImVec2 size;
  DinoSurface surface;
  DinoMovementModifier movement_modifier;
  DinoAction action;
  DinoVulnerability vulnerability;
};

struct Obstacle {
  ImVec2 pos;
  ImVec2 size;
};

class Game {
public:
  Game();
  ~Game() = default;
  void render_frame();
  void move_dino();
  bool check_collision(Obstacle obstacle);
  void make_obstacle();

private:
  Dino dino;
  GameVars vars;
  std::vector<Obstacle> obstacles;
  ImVec2 canvas_size;
};
