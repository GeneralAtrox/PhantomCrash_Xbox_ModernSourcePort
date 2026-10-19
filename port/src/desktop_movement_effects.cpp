#include "desktop_movement_effects.hpp"
#include <SDL3/SDL_log.h>

namespace phantomcrash {
namespace {
Vec3 reflected(Vec3 direction) {
  direction.y = -direction.y; // 0x08814a mode 1
  return direction;
}
} // namespace

void DesktopMovementEffects::stop(Watch &watch, EffectSystem &effects, const std::size_t first,
                                  const std::size_t end) {
  for (std::size_t slot = first; slot < end; ++slot) {
    for (auto &handle : watch.handles[slot]) {
      effects.stop(handle, true);
      handle = 0;
    }
  }
}

int DesktopMovementEffects::start(Watch &watch, const RobotCombat &robot, EffectSystem &effects,
                                  const std::size_t slot, const std::size_t lane, const int group,
                                  const bool reflect) {
  const auto point = robot.movement_point(slot);
  if (!point || group < 0) {
    return 0;
  }
  auto &handle = watch.handles[slot][lane];
  effects.stop(handle, true);
  handle = effects.start(group, point->position, {},
                         reflect ? reflected(point->direction) : point->direction);
  return handle != 0 ? 1 : 0;
}

void DesktopMovementEffects::tick(const Battle &battle, EffectSystem &effects,
                                  const float seconds) {
  feet_.tick(battle, effects, seconds);
  const auto &robots = battle.robots();
  for (std::size_t i = robots.size(); i < robots_.size(); ++i) {
    stop(robots_[i], effects, 0, movement_slots);
  }
  robots_.resize(robots.size());
  for (std::size_t i = 0; i < robots.size(); ++i) {
    const auto &robot = robots[i];
    auto &watch = robots_[i];
    const auto &mover = robot.mover;
    const auto mode = mover.mode();
    const bool active =
        robot.alive && robot.pose && mode != MoveMode::scripted && mode != MoveMode::hit;
    if (watch.owner != robot.pose.get() || !active) {
      stop(watch, effects, 0, movement_slots);
      watch = {};
      watch.owner = robot.pose.get();
    }
    if (!active) {
      watch.grounded = mover.grounded();
      watch.mode = mode;
      continue;
    }
    const auto &pose = *robot.pose;
    const auto &groups = pose.movement_groups();
    int started = 0;
    const char *event = nullptr;
    // Walking off a ledge does not ignite jump jets.
    if (watch.grounded && !mover.grounded() && mover.velocity().y > 0) {
      stop(watch, effects, 4, 8);
      event = "jump";
      for (std::size_t slot = 4; slot < 6; ++slot) {
        started += start(watch, pose, effects, slot, 0, groups.jump);
      }
    }
    if (mover.down_boosting() && !watch.descent) {
      stop(watch, effects, 4, 6);
      event = "descent";
      for (std::size_t slot = 6; slot < 8; ++slot) {
        started += start(watch, pose, effects, slot, 0, groups.descent);
      }
    }
    if (mover.dodged() != 0 &&
        (watch.mode != MoveMode::dodge || mover.mode_seconds() < watch.mode_seconds)) {
      event = "dodge";
      const std::size_t slot = mover.dodged() < 0 ? 4 : 5;
      started += start(watch, pose, effects, slot, 0, groups.dodge);
      started += start(watch, pose, effects, slot, 1, groups.dodge, true);
    }
    if (mode == MoveMode::dash && watch.mode != MoveMode::dash) {
      event = "dash";
      for (std::size_t slot = 8; slot < 13; ++slot) {
        for (std::size_t lane = 0; lane < groups.dash.size(); ++lane) {
          started += start(watch, pose, effects, slot, lane, groups.dash[lane]);
        }
      }
    } else if (watch.mode == MoveMode::dash && mode != MoveMode::dash) {
      stop(watch, effects, 8, 13);
      for (std::size_t slot = 8; slot < 13; ++slot) {
        start(watch, pose, effects, slot, 2, groups.dash_stop);
      }
    }
    if (mover.grounded() && !watch.grounded) {
      stop(watch, effects, 4, 8);
      if (robot.piloted) {
        SDL_Log("Movement effects: robot %zu landed; thrust emission stopped", i);
      }
    }
    for (std::size_t slot = 4; slot < 13; ++slot) {
      const auto point = pose.movement_point(slot);
      if (!point) {
        stop(watch, effects, slot, slot + 1);
        continue;
      }
      for (std::size_t lane = 0; lane < watch.handles[slot].size(); ++lane) {
        auto &handle = watch.handles[slot][lane];
        if (!effects.alive(handle)) {
          handle = 0;
          continue;
        }
        effects.move(handle, point->position);
        effects.set_direction(handle, slot < 6 && lane == 1 ? reflected(point->direction)
                                                            : point->direction);
      }
    }
    if (event && robot.piloted) {
      SDL_Log("Movement effects: robot %zu %s, %d groups, vertical speed %.2f", i, event, started,
              mover.velocity().y);
    }
    if (event && started == 0 && !watch.warned_missing) {
      SDL_Log("Movement effects: robot %zu has no usable %s mounts/effect groups", i, event);
      watch.warned_missing = true;
    }
    watch.grounded = mover.grounded();
    watch.descent = mover.down_boosting();
    watch.mode = mode;
    watch.mode_seconds = mover.mode_seconds();
  }
}
} // namespace phantomcrash
