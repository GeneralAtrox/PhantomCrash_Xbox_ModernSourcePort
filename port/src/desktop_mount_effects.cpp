#include "desktop_mount_effects.hpp"

#include <algorithm>

namespace phantomcrash {
namespace {
constexpr float tick_seconds = 1.0F / 30.0F; // 0x07973f runs once a battle tick
constexpr int max_ticks_per_frame = 8;

// The effect groups (tables/effects.json).
constexpr int spark_group = 0x26;    // 0x086f69
constexpr int arc_group = 0x27;      // 0x087063, to the forearm
constexpr int trail_group = 0x28;    // 0x0874c3
constexpr int side_arc_group = 0x29; // 0x087063, out to the side
constexpr int burst_group = 0x2a;    // 0x086e91, once
constexpr std::array<int, 2> burst_lane_groups{0x2b, 0x2c};

constexpr int knocked_ticks = 15; // 0x08d686: robot +0x9c + slot * 4
constexpr int restart_ticks = 10; // 0x07973f, when the sparks end
constexpr int pause_floor = 5;
// The longest random pause, in ticks, before a lane starts again
// (0x2326e0, by lane set and lane).
constexpr std::array<std::array<float, 4>, 4> longest_pause{{
    {5, 15, 5, 25},
    {5, 15, 5, 25},
    {5, 15, 5, 5},
    {5, 15, 5, 5},
}};
constexpr float trail_top_kmh = 350.0F;      // 0x19b588
constexpr float kmh_per_unit_second = 3.6F;  // 0x17ccfc * 0x1b5adc * 0x19f8d0, a frame at 30
constexpr float arc_jitter = 0.1F;           // 0x087063
constexpr float arc_reach = 0.7F;            // of the way to the forearm
constexpr float stump_spark_midpoint = 0.5F; // 0x07a304: joint + forearm, then 0x065640
constexpr float side_arc_spread = 0.8F;
constexpr float side_arc_shortest = 0.3F;
constexpr float side_arc_longest = 0.7F;
constexpr float side_arc_nearest = 0.7F; // of its length out along itself
} // namespace

float DesktopMountEffects::random(const float low, const float high) {
  return std::uniform_real_distribution<float>(low, high)(random_);
}

int DesktopMountEffects::pause(const float longest, const int floor) {
  return static_cast<int>(random(0.0F, longest)) + floor;
}

void DesktopMountEffects::stop(Lanes &lanes, EffectSystem &effects, const int wait) {
  for (Lane &lane : lanes) {
    if (lane.handle != 0) {
      effects.stop(lane.handle, true);
      lane.handle = 0;
      lane.wait = wait;
    }
  }
}

std::optional<DesktopMountEffects::Arm> DesktopMountEffects::arm(const RobotCombat &pose,
                                                                 const std::size_t side) {
  const PosePart &part = pose.pose()[side + 1];
  if (!part.set[0] || !part.set[1]) {
    return std::nullopt;
  }
  return Arm{part.bones[0], transform_point({}, part.bones[1])};
}

bool DesktopMountEffects::keep(Lane &lane, EffectSystem &effects, const int group, const Vec3 at) {
  if (lane.handle == 0) {
    lane.handle = effects.start(group, at);
    return lane.handle != 0;
  }
  if (!effects.alive(lane.handle)) {
    lane.handle = 0;
    return false;
  }
  effects.move(lane.handle, at);
  return true;
}

bool DesktopMountEffects::keep_arc(Lane &lane, EffectSystem &effects, const Arm &arm,
                                   const bool side_arc, const bool outward) {
  if (lane.handle == 0) {
    lane.jitter = {random(-arc_jitter, arc_jitter), random(-arc_jitter, arc_jitter),
                   random(-arc_jitter, arc_jitter)};
  } else if (!effects.alive(lane.handle)) {
    lane.handle = 0;
    return false;
  }
  const Vec3 origin = transform_point(lane.jitter, arm.joint);
  Vec3 segment = (arm.forearm - transform_point({}, arm.joint)) * arc_reach;
  Vec3 at = origin;
  if (side_arc) {
    // Along the joint's x axis, scattered and re-chosen every tick.
    const Vec3 axis{arm.joint.at(0, 0) + random(-side_arc_spread, side_arc_spread),
                    arm.joint.at(0, 1) + random(-side_arc_spread, side_arc_spread),
                    arm.joint.at(0, 2) + random(-side_arc_spread, side_arc_spread)};
    segment = normalize(axis) * random(side_arc_shortest, side_arc_longest);
    if (outward) {
      segment = segment * -1.0F;
    }
    at = origin + segment * random(side_arc_nearest, 1.0F);
  }
  if (lane.handle == 0) {
    lane.handle = effects.start(side_arc ? side_arc_group : arc_group, at, {}, segment);
    if (lane.handle == 0) {
      return false;
    }
  } else {
    effects.move(lane.handle, at);
  }
  effects.set_segment(lane.handle, segment);
  return true;
}

void DesktopMountEffects::step_stump(const std::size_t side, const Arm &arm, const float speed,
                                     Watch &watch, EffectSystem &effects) {
  const Vec3 joint = transform_point({}, arm.joint);
  const Vec3 spark_position = (joint + arm.forearm) * stump_spark_midpoint;
  Lanes &forearm = watch.lanes[side];
  Lanes &shoulder = watch.lanes[side + 2];
  for (std::size_t index = 0; index < 4; ++index) {
    Lane &lane = forearm[index];
    if (lane.handle == 0 && lane.wait != 0) {
      --lane.wait;
    } else if (index == 0) {
      // The trail restarts as soon as it ends; fast, it stops.
      if (speed >= trail_top_kmh) {
        effects.stop(lane.handle, true);
        lane.handle = 0;
      } else {
        keep(lane, effects, trail_group, joint);
      }
    } else {
      const bool playing = index == 3 ? keep_arc(lane, effects, arm, false, false)
                                      : keep(lane, effects, spark_group, spark_position);
      if (!playing) {
        lane.wait = pause(longest_pause[side][index], pause_floor);
      }
    }
    Lane &joint_lane = shoulder[index];
    if (joint_lane.handle == 0 && joint_lane.wait != 0) {
      --joint_lane.wait;
      continue;
    }
    // The left arm's side arcs point the other way (0x087063's set index).
    const bool playing = index == 3 ? keep_arc(joint_lane, effects, arm, true, side == 0)
                                    : keep(joint_lane, effects, spark_group, joint);
    if (!playing) {
      joint_lane.wait = index == 3 ? pause(longest_pause[side + 2][index], 0)
                                   : pause(longest_pause[side + 2][index], pause_floor);
    }
  }
}

void DesktopMountEffects::step(const Combatant &robot, Watch &watch, EffectSystem &effects) {
  const RobotCombat &pose = *robot.pose;
  const float speed = length(robot.mover.velocity()) * kmh_per_unit_second;
  for (std::size_t slot = 0; slot < watch.mounts.size(); ++slot) {
    Mount &mount = watch.mounts[slot];
    const bool on_arm = slot < 2;
    const std::size_t side = on_arm ? slot : slot - 2;
    // The lanes the burst plays in: the forearm's, or the shoulder weapon's.
    Lanes &burst = watch.lanes[on_arm ? side : side + 4];
    if (!robot.lost[slot]) {
      if (mount.state != 0) {
        stop(burst, effects);
        if (on_arm) {
          stop(watch.lanes[side + 2], effects);
        }
        mount = {};
      }
      continue;
    }
    if (mount.state == 0) {
      mount = {1, knocked_ticks};
    }
    const std::optional<Arm> limb = on_arm ? arm(pose, side) : std::nullopt;
    const Vec3 at = limb ? limb->forearm : transform_point({}, pose.shoulders()[side]);
    switch (mount.state) {
    case 1:
      if (limb) {
        for (std::size_t index = 0; index < 3; ++index) {
          keep(burst[index], effects, spark_group, at);
        }
        keep_arc(burst[3], effects, *limb, false, false);
      }
      if (mount.count-- < 1) {
        stop(burst, effects, restart_ticks);
        mount.state = 2;
      }
      break;
    case 2:
      for (std::size_t index = 0; index < burst_lane_groups.size(); ++index) {
        burst[index].handle = effects.start(burst_lane_groups[index], at);
      }
      effects.start(burst_group, at);
      mount.state = 3;
      break;
    case 3: {
      bool any = false;
      for (std::size_t index = 0; index < burst_lane_groups.size(); ++index) {
        Lane &lane = burst[index];
        if (lane.handle != 0 && effects.alive(lane.handle)) {
          effects.move(lane.handle, at);
          any = true;
        } else {
          lane.handle = 0;
        }
      }
      if (!any) {
        mount.state = 4;
      }
      break;
    }
    default:
      if (limb) {
        step_stump(side, *limb, speed, watch, effects);
      }
      break;
    }
  }
}

void DesktopMountEffects::tick(const Battle &battle, EffectSystem &effects, const float seconds) {
  const auto &robots = battle.robots();
  for (std::size_t i = robots.size(); i < robots_.size(); ++i) {
    for (Lanes &lanes : robots_[i].lanes) {
      stop(lanes, effects);
    }
  }
  robots_.resize(robots.size());
  owed_ = std::min(owed_ + std::max(seconds, 0.0F), tick_seconds * max_ticks_per_frame);
  while (owed_ >= tick_seconds) {
    owed_ -= tick_seconds;
    for (std::size_t i = 0; i < robots.size(); ++i) {
      const Combatant &robot = robots[i];
      Watch &watch = robots_[i];
      // A new robot, or a wreck: 0x07973f stops every lane once the robot
      // is down (robot +0x40).
      if (watch.owner != robot.pose.get() || !robot.alive) {
        for (Lanes &lanes : watch.lanes) {
          stop(lanes, effects);
        }
        const bool same = watch.owner == robot.pose.get();
        const auto mounts = watch.mounts;
        watch = {};
        watch.owner = robot.pose.get();
        if (same) {
          watch.mounts = mounts; // a wreck's mounts stay as they were, silent
        }
      }
      if (robot.pose && robot.alive) {
        step(robot, watch, effects);
      }
    }
  }
}
} // namespace phantomcrash
