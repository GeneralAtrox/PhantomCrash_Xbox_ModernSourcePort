#include "desktop_battle_view.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>

namespace phantomcrash {
namespace {

// The node of mesh `mesh` in a converted model ("mesh<m>_node0"), or -1.
int mesh_node(const ModelData *model, const int mesh) {
  if (model == nullptr || mesh < 0) {
    return -1;
  }
  const std::string name = "mesh" + std::to_string(mesh) + "_node0";
  for (std::size_t node = 0; node < model->nodes.size(); ++node) {
    if (model->nodes[node].name == name) {
      return static_cast<int>(node);
    }
  }
  return -1;
}

constexpr float half_strength = 0.5F; // a camouflage-hidden volume goes from here

} // namespace

bool project_point(const Mat4 &view_projection, const Vec3 point, const int width, const int height,
                   ScreenPoint &out) {
  const auto &m = view_projection;
  const float x = point.x * m.at(0, 0) + point.y * m.at(1, 0) + point.z * m.at(2, 0) + m.at(3, 0);
  const float y = point.x * m.at(0, 1) + point.y * m.at(1, 1) + point.z * m.at(2, 1) + m.at(3, 1);
  const float w = point.x * m.at(0, 3) + point.y * m.at(1, 3) + point.z * m.at(2, 3) + m.at(3, 3);
  if (w <= 0.05F) {
    return false;
  }
  out.x = (x / w * 0.5F + 0.5F) * static_cast<float>(width);
  out.y = (0.5F - y / w * 0.5F) * static_cast<float>(height);
  out.depth = w;
  return true;
}

std::unique_ptr<BattleView::Visual> BattleView::make_visual(const RobotParts &parts) const {
  auto visual = std::make_unique<Visual>();
  visual->assembly = assemble_robot(robots_.catalogue(), robots_.skeletons(), parts);
  visual->combat = std::make_shared<RobotCombat>(robots_, visual->assembly);
  visual->parts = std::make_unique<RobotVisual>(world_, visual->assembly);
  visual->ground_node = mesh_node(world_.model_data(ground_model_), visual->assembly.shadow_mesh);
  return visual;
}

void BattleView::add(const RobotParts &parts) {
  visuals_.push_back(make_visual(parts));
}

void BattleView::replace(const std::size_t index, const RobotParts &parts) {
  if (index < visuals_.size()) {
    visuals_[index] = make_visual(parts);
  }
}

void BattleView::preload(const std::vector<RobotParts> &looks) {
  const auto start = std::chrono::steady_clock::now();
  for (const RobotParts &parts : looks) {
    try {
      static_cast<void>(make_visual(parts)); // the world keeps what it loaded
    } catch (const std::exception &) {
      // replace draws a stand-in for a part out of range
    }
  }
  const std::chrono::duration<double, std::milli> took = std::chrono::steady_clock::now() - start;
  SDL_Log("Robots: %zu possible newcomers loaded ahead in %.0f ms", looks.size(), took.count());
}

MovementTuning BattleView::movement(const std::size_t index, const MovementTuning &base) const {
  if (index >= visuals_.size()) {
    return base;
  }
  return stride_tuning(robots_, visuals_[index]->assembly, base);
}

float BattleView::radius(const std::size_t index, const float fallback) const {
  if (index >= visuals_.size() || visuals_[index]->assembly.radius <= 0.0F) {
    return fallback;
  }
  return visuals_[index]->assembly.radius;
}

int BattleView::legs_model(const std::size_t index) const {
  return index < visuals_.size() ? visuals_[index]->assembly.leg_type : -1;
}

std::optional<Vec3> BattleView::torso(const std::size_t index) const {
  if (index >= visuals_.size()) {
    return std::nullopt;
  }
  return visuals_[index]->parts->torso(visuals_[index]->pose);
}

void BattleView::set_shield(const EntryShieldLook &look) {
  shield_ = look;
  shield_model_ = world_.model(look.model);
}

void BattleView::set_shadows(const RobotShadowLook &look, const bool split_screen) {
  shadows_ = look;
  split_screen_ = split_screen;
  ground_model_ = look.ground_model.empty() ? -1 : world_.model(look.ground_model);
  for (const auto &visual : visuals_) {
    visual->ground_node = mesh_node(world_.model_data(ground_model_), visual->assembly.shadow_mesh);
  }
}

void BattleView::add_shadow(const Combatant &robot, const Visual &visual, const Mat4 &root,
                            const std::size_t first, std::vector<WorldItem> &items) const {
  // A stencil volume cannot fade: it goes once the camouflage is half on.
  const float strength = robot_shadow_strength(shadows_, robot.camouflage_sweep);
  switch (robot_shadow(shadows_, robot.piloted, split_screen_, robot.mover.grounded())) {
  case RobotShadow::volume:
    for (std::size_t item = first; item < items.size(); ++item) {
      items[item].casts_shadow = strength >= half_strength;
    }
    break;
  case RobotShadow::ground:
    if (ground_model_ >= 0 && visual.ground_node >= 0 && strength > 0.0F) {
      WorldItem shadow;
      shadow.model = ground_model_;
      shadow.node = visual.ground_node;
      shadow.world = root;
      std::array<float, 4> colour = shadows_.ground_colour;
      colour[3] *= strength;
      shadow.colour = colour;
      shadow.ground_shadow = true;
      shadow.double_sided = true;
      items.push_back(std::move(shadow));
    }
    break;
  case RobotShadow::none:
    break;
  }
}

void BattleView::bind(Battle &battle) {
  for (std::size_t i = 0; i < visuals_.size() && i < battle.robots().size(); ++i) {
    auto &robot = battle.robot(static_cast<int>(i));
    if (battle.cooperative() && robot.pose) {
      visuals_[i]->combat = robot.pose;
    }
    if (!battle.simulation_poses() && robot.pose != visuals_[i]->combat) {
      robot.pose = visuals_[i]->combat;
      std::array<Vec3, 4> targets;
      targets.fill(robot.aim_origin() +
                   heading_forward(robot.mover.aim_heading()) * robot.pose->aim_distance());
      robot.pose->tick(robot.mover, {}, robot.lost, targets, 0);
    }
    // Online confirmed playback ticks older states, then binds the predicted
    // present. Drawing must use that present's skeleton too, or its bodies
    // stand where the shoulder mounts and weapons no longer are.
    if (visuals_[i]->combat) {
      visuals_[i]->pose = visuals_[i]->combat->pose();
    }
  }
}

void BattleView::tick(Battle &battle, const float seconds) {
  bind(battle);
  sync_wrecks(battle);
  advance_shield(battle, seconds);
  const auto &robots = battle.robots();
  for (std::size_t index = 0; index < visuals_.size() && index < robots.size(); ++index) {
    const auto &robot = robots[index];
    auto &visual = *visuals_[index];
    if (visual.motion_revision != robot.mover.motion_revision()) {
      if (robot.piloted || robot.dying) {
        SDL_Log("Robot animation: robot=%zu motion=%d revision=%llu frame=%.2f speed=%.3f "
                "loop=%d arms=%.2f/%.2f",
                index, robot.mover.motion(),
                static_cast<unsigned long long>(robot.mover.motion_revision()),
                visual.combat->motion_frame(), robot.mover.motion_speed(),
                robot.mover.motion_loops(), visual.combat->arm_weight(0),
                visual.combat->arm_weight(1));
      }
      visual.motion_revision = robot.mover.motion_revision();
    }
    if (visuals_[index]->dying != robots[index].dying) {
      SDL_Log("Death animation: robot=%zu %s motion=%d elapsed=%.3f frame=%.2f arms=%.2f/%.2f",
              index, robots[index].dying ? "started" : "finished", robot_motion::death,
              robots[index].death_seconds, visual.combat->motion_frame(),
              visual.combat->arm_weight(0), visual.combat->arm_weight(1));
      visuals_[index]->dying = robots[index].dying;
    }
    visuals_[index]->pose = visuals_[index]->combat->pose();
    std::array<bool, 4> fired{};
    for (const auto &event : battle.events()) {
      if (event.kind == BattleEventKind::fired && event.robot == static_cast<int>(index) &&
          event.slot >= 0 && event.slot < 4) {
        fired[static_cast<std::size_t>(event.slot)] = true;
      }
    }
    visual.parts->animate(robot.mover.motion(), visual.combat->motion_frame(),
                          robot.mover.motion_loops(), seconds, fired, robot.mover.motion_revision(),
                          robot.mover.motion_speed());
  }
}

std::optional<Mat4> BattleView::source_weapon_frame(const Battle &battle, const int owner,
                                                    const int slot, const int node) const {
  if (owner < 0 || owner >= static_cast<int>(visuals_.size()) ||
      owner >= static_cast<int>(battle.robots().size())) {
    return std::nullopt;
  }
  const auto &robot = battle.robots()[static_cast<std::size_t>(owner)];
  const auto &visual = *visuals_[static_cast<std::size_t>(owner)];
  const Mat4 root = rotation_y(robot.mover.heading()) * translation(robot.mover.position());
  return visual.parts->source_weapon_frame(visual.pose, root, visual.combat->shoulders(), slot,
                                           node);
}

SourceMotionClock BattleView::source_motion_clock(const std::size_t owner) const {
  if (owner >= visuals_.size() || !visuals_[owner]->combat) {
    return {};
  }
  const auto &visual = *visuals_[owner];
  const int motion = visual.combat->motion();
  const auto &actions = visual.assembly.source_motion_actions;
  if (motion < 0 || motion >= static_cast<int>(actions.size()) ||
      motion >= static_cast<int>(visual.assembly.timings.size())) {
    return {};
  }
  return {actions[motion], visual.combat->motion_frame(), visual.assembly.timings[motion].last, 0};
}

std::optional<Mat4> BattleView::source_model_frame(const Battle &battle, const int owner,
                                                   const int part, const int node) const {
  if (owner < 0 || owner >= static_cast<int>(visuals_.size()) ||
      owner >= static_cast<int>(battle.robots().size()) || node < 0) {
    return std::nullopt;
  }
  const auto &visual = *visuals_[owner];
  if (part == 0) {
    const auto &controls = visual.pose[0].source_controls;
    return node < static_cast<int>(controls.size()) ? controls[node] : std::nullopt;
  }
  if (part == 1) {
    const auto &robot = battle.robots()[static_cast<std::size_t>(owner)];
    return visual.parts->source_body_frame(
        visual.pose, rotation_y(robot.mover.heading()) * translation(robot.mover.position()), node);
  }
  return std::nullopt;
}

std::optional<Mat4> BattleView::source_impact_frame(const Battle &battle, const int owner) const {
  if (owner < 0 || owner >= static_cast<int>(visuals_.size()) ||
      owner >= static_cast<int>(battle.robots().size())) {
    return std::nullopt;
  }
  const auto &pose = visuals_[static_cast<std::size_t>(owner)]->pose[0];
  if (pose.source_model_root) {
    return pose.source_model_root;
  }
  // Phantom Crash targets have no S.L.A.I. control zero. Their actor root is
  // the mixed-game attachment adapter; the torso is a separate aiming frame.
  const auto &robot = battle.robots()[static_cast<std::size_t>(owner)];
  return rotation_y(robot.mover.heading()) * translation(robot.mover.position());
}

void BattleView::add_items(const Battle &battle, std::vector<WorldItem> &items, const int hidden,
                           const int hidden_shield) {
  add_wreck_items(battle, items);
  const auto &robots = battle.robots();
  for (std::size_t index = 0; index < visuals_.size() && index < robots.size(); ++index) {
    const Combatant &robot = robots[index];
    if ((!robot.alive && !robot.dying) || static_cast<int>(index) == hidden) {
      continue;
    }
    Visual &visual = *visuals_[index];
    const RobotMover &mover = robot.mover;
    const Mat4 root = rotation_y(mover.heading()) * translation(mover.position());
    const std::size_t first = items.size();
    visual.parts->add_items(visual.pose, root, items, &visual.combat->shoulders(), robot.lost);
    const float flash = robot.hit_flash > 0.0F ? 1.0F : 0.0F;
    std::optional<OpticalSphere> optical;
    if (robot.camouflage_sweep > 0.0F) {
      // 0x074e80 measures from robot +0x454c, a copy of +0x2790 (0x077406):
      // taken, as for the gates, as the lower collision sphere's centre.
      optical = optical_sphere(optical_, robot.aim_origin(), robot.camouflage_sweep);
    }
    for (std::size_t item = first; item < items.size(); ++item) {
      items[item].optical = optical;
      items[item].flash = flash;
    }
    add_shadow(robot, visual, root, first, items);
    if (static_cast<int>(index) != hidden_shield &&
        (!robot.piloted || robot.mover.mode() != MoveMode::scripted)) {
      add_shield(robot, items);
    }
    for (std::size_t item = first; item < items.size(); ++item) {
      items[item].robot = static_cast<int>(index);
    }
  }
}

// The shared shield angle, rainbow phase and UV scroll: the original steps
// them at every shield draw (0x06f592, 0x0c43ae), 30 draws a second for each
// shield shown, whatever the number of rendered views here.
void BattleView::advance_shield(const Battle &battle, const float seconds) {
  float shields = 1.0F;
  if (shield_.per_shield) {
    shields = static_cast<float>(std::ranges::count_if(
        battle.robots(), [](const Combatant &robot) { return robot.shield_glow > 0.0F; }));
  }
  const float draws = std::max(seconds, 0.0F) * shield_.update_hz * shields;
  shield_spin_ =
      std::fmod(shield_spin_ + shield_.spin_per_second / shield_.update_hz * draws, 2.0F * pi);
  // The phase restarts at 1 below -1 (and at -1 above 1 for a rising one).
  shield_phase_ = std::fmod(shield_phase_ + shield_.phase_step * draws + 1.0F, 2.0F);
  shield_phase_ += shield_phase_ < 0.0F ? 1.0F : -1.0F;
  shield_scroll_ += shield_.scroll_step * draws;
  shield_scroll_ -= std::floor(shield_scroll_);
}

// 0x06f592: the sphere around the robot's lower collision sphere, turning,
// its colour's alpha times the glow, added to what is behind it.
void BattleView::add_shield(const Combatant &robot, std::vector<WorldItem> &items) const {
  if (shield_model_ < 0 || robot.shield_glow <= 0.0F) {
    return;
  }
  WorldItem shell;
  shell.model = shield_model_;
  const float size = robot.radius * shield_.scale;
  shell.world = scaling({size, size, size}) * rotation_y(robot.mover.heading() + shield_spin_) *
                translation(robot.aim_origin());
  shell.colour = shield_.colour;
  shell.alpha = robot.shield_glow;
  shell.rainbow = std::array{shield_phase_, shield_scroll_};
  shell.additive = true;
  items.push_back(std::move(shell));
}

} // namespace phantomcrash
