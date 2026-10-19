#include "phantomcrash/battle.hpp"
#include "phantomcrash/robot_combat.hpp"

#include <algorithm>

namespace phantomcrash {

void Battle::tick_death(const int index, const float seconds) {
  Combatant &robot = robots_[static_cast<std::size_t>(index)];
  if (!robot.dying) {
    return;
  }
  float floor = stage_.ground_height(robot.mover.position(), ground_snap, robot.radius);
  if (objects_) {
    floor =
        std::max(floor, objects_->ground_height(robot.mover.position(), ground_snap, robot.radius));
  }
  floor = std::max(floor, wrecks_.ground_height(robot.mover.position(), ground_snap, robot.radius));
  robot.mover.set_floor(floor);
  robot.mover.tick({}, seconds);
  collide(index, seconds);
  pose_robot(index, seconds);
  robot.death_seconds += seconds;
  const bool finished = robot.pose ? robot.pose->death_finished()
                                   : robot.death_seconds >= settings_.death_fallback_seconds;
  if (finished && robot.mover.grounded()) {
    if (robot.pose) {
      RobotWreck wreck;
      wreck.assembly = robot.pose->assembly();
      wreck.pose = robot.pose->pose();
      for (auto &part : wreck.pose) {
        part.contacts.clear();
      }
      wreck.shoulders = robot.pose->shoulders();
      wreck.root = rotation_y(robot.mover.heading()) * translation(robot.mover.position());
      wreck.lost = robot.lost;
      wreck.centre = robot.aim_origin();
      wreck.radius = robot.radius;
      wreck.burn_left = std::max(0.0F, settings_.robot_wrecks.burn_seconds - robot.death_seconds);
      wrecks_.add(std::move(wreck));
    }
    robot.dying = false;
  }
}

} // namespace phantomcrash
