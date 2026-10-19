#include "desktop_online_assets.hpp"
#include "desktop_wreck_collision.hpp"
#include "phantomcrash/robot_combat.hpp"
#include <stdexcept>

namespace phantomcrash {

DesktopMatchAssets::DesktopMatchAssets(RobotLibrary &robots, WorldRenderer &world)
    : robots_(robots), world_(world) {
  if (!robots_.available()) {
    throw std::runtime_error("Online play requires converted robot assets");
  }
}

void DesktopMatchAssets::prepare(CoopMatch &match) {
  auto &battle = *match.battle;
  battle.own_simulation_poses();
  if (!battle.wrecks().simulation_collision()) {
    battle.wrecks().set_collision_builder([world = &world_](const RobotWreck &wreck) {
      return build_wreck_collision(*world, wreck);
    });
  }
  match.simulation_looks.resize(battle.robots().size());
  for (std::size_t index = 0; index < battle.robots().size(); ++index) {
    auto &robot = battle.robot(static_cast<int>(index));
    const auto look = match.look(static_cast<int>(index));
    if (robot.pose && match.simulation_looks[index] == look) {
      continue;
    }
    auto assembly = assemble_robot(robots_.catalogue(), robots_.skeletons(), look);
    // Collision must never depend on whether a missing GPU model happened to
    // have been requested by a local view. Require every named model up front.
    const auto require_model = [&](const std::string &path) {
      if (!path.empty() && !world_.model_data(world_.model(path))) {
        throw std::runtime_error("Online robot model could not load: " + path);
      }
    };
    require_model(assembly.body_model);
    require_model(assembly.legs_model);
    require_model(assembly.emblem_model);
    for (const auto &path : assembly.arm_models) {
      require_model(path);
    }
    for (const auto &path : assembly.shoulder_models) {
      require_model(path);
    }
    battle.set_radius(static_cast<int>(index),
                      assembly.radius > 0 ? assembly.radius : battle.settings().robot_radius);
    robot.pose = std::make_shared<RobotCombat>(robots_, std::move(assembly));
    robot.mover.set_timings(&robot.pose->assembly().timings, robot.pose->assembly().leg_type);
    std::array<Vec3, weapon_slots> targets;
    targets.fill(robot.aim_origin() +
                 heading_forward(robot.mover.aim_heading()) * robot.pose->aim_distance());
    robot.pose->tick(robot.mover, {}, robot.lost, targets, 0);
    match.simulation_looks[index] = look;
  }
}

} // namespace phantomcrash
