#include "phantomcrash/robot_combat.hpp"
#include "phantomcrash/skeleton.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace phantomcrash {
namespace {

// The dash's clips, straight and leaning (2-4), and the dash back's (5-7).
int dash_family(const int motion) {
  if (motion >= robot_motion::dash && motion < robot_motion::dash_back) {
    return 0;
  }
  if (motion >= robot_motion::dash_back && motion < robot_motion::turn_right) {
    return 1;
  }
  return -1;
}

} // namespace

RobotCombat::RobotCombat(RobotLibrary &library, RobotAssembly assembly)
    : assembly_(std::move(assembly)), animator_(library, assembly_),
      tuning_(library.combat_animation()) {
  for (std::size_t slot = 0; slot < 4; ++slot) {
    const auto &path = slot < 2 ? assembly_.arm_models[slot] : assembly_.shoulder_models[slot - 2];
    if (const auto it = library.weapon_mounts().find(path); it != library.weapon_mounts().end()) {
      sockets_[slot] = it->second;
    }
  }
  const auto &movement = library.movement_mounts();
  movement_groups_ = movement.groups;
  if (assembly_.leg_type >= 0 && assembly_.leg_type < static_cast<int>(movement.legs.size())) {
    movement_mounts_ = movement.legs[static_cast<std::size_t>(assembly_.leg_type)];
  }
  animator_.play(robot_motion::stand, true, 0);
}

std::optional<MovementPoint> RobotCombat::movement_point(const std::size_t slot) const {
  if (slot >= movement_slots || movement_mounts_[slot].kind < 0 ||
      (slot == 8 && assembly_.body_type == 1)) {
    return std::nullopt;
  }
  for (const auto &part : pose_) {
    if (const auto &bone = part.movement_mounts[slot]) {
      const auto &point = movement_mounts_[slot];
      const Vec3 position = transform_point(point.position, *bone);
      const Vec3 direction =
          normalize(transform_point(point.direction, *bone) - transform_point({}, *bone));
      return MovementPoint{position, direction};
    }
  }
  return std::nullopt;
}

void RobotCombat::reset() {
  animator_.reset_procedural_clock();
  animator_.play(robot_motion::stand, true, 0.0F, 0.0F, true);
  motion_revision_.reset();
  weights_.fill(0);
  barrel_.fill(0);
  auxiliary_.fill(0);
  wheel_turn_ = 0;
  for (auto &muzzles : muzzles_) {
    muzzles.clear();
  }
}

void RobotCombat::tick(const RobotMover &mover, const std::array<bool, 4> &firing,
                       const std::array<bool, 4> &lost, const std::array<Vec3, 4> &targets,
                       const float seconds, const std::array<int, 4> &aim_modes,
                       const bool piloted) {
  if (motion_revision_ != mover.motion_revision() || animator_.motion() != mover.motion()) {
    // A dash leaning into a turn changes clip mid-stride (0x08f5d8): its
    // frame carries over, blended as any other change.
    const int family = dash_family(mover.motion());
    const bool lean = family >= 0 && family == dash_family(animator_.motion()) &&
                      animator_.motion() != mover.motion();
    animator_.play(mover.motion(), mover.motion_loops(), tuning_.fade_frames,
                   lean ? animator_.frame() : 0.0F, true);
    motion_revision_ = mover.motion_revision();
  } else if (mover.motion_loops() != animator_.looping()) {
    animator_.play(mover.motion(), mover.motion_loops());
  }
  // Set after any change above, so the motion it froze keeps the old value.
  const bool keep_sway = mover.mode() == MoveMode::stand || mover.mode() == MoveMode::walk;
  animator_.set_keep_sway(keep_sway);
  animator_.set_speed(mover.motion_speed());
  animator_.tick(seconds);
  const bool destroyed = mover.mode() == MoveMode::destroyed;
  if (destroyed) {
    auxiliary_.fill(0);
  } else {
    advance_auxiliary(mover, firing, piloted, seconds);
  }
  animator_.set_auxiliary_frames(auxiliary_);
  advance_wheels(mover, seconds);
  animator_.set_wheel_turn(wheel_turn_);
  animator_.advance_source_roll(mover, seconds);
  std::array<ArmAim, 2> arms;
  for (std::size_t side = 0; side < 2; ++side) {
    // A strike lowers the aim so the arms play their strike motions.
    const bool raised = firing[side] && !lost[side] && mover.mode() != MoveMode::scripted &&
                        mover.mode() != MoveMode::strike;
    const float change = seconds / tuning_.raise_seconds;
    weights_[side] = lost[side] || destroyed
                         ? 0
                         : std::clamp(weights_[side] + (raised ? change : -change), 0.0F, 1.0F);
    arms[side] = {weights_[side], targets[side], tuning_.joint_pitch_limit};
  }
  const Mat4 root = rotation_y(mover.heading()) * translation(mover.position());
  animator_.pose(root, mover.twist(), mover.pitch(), keep_sway, pose_, arms, tuning_.arm_poses);
  const Mat4 torso = pose_[0].mount.value_or(root);
  // The shoulder weapons tilt with the body's aim, so they share its clamp.
  const float shoulder_pitch = std::clamp(mover.pitch(), -aim_pitch_limit, aim_pitch_limit);
  for (std::size_t side = 0; side < 2; ++side) {
    shoulders_[side] =
        translation(assembly_.shoulder_shifts[side]) * rotation_x(shoulder_pitch) * torso;
    if (!destroyed && !lost[side + 2] && (aim_modes[side + 2] == 0 || aim_modes[side + 2] == 1)) {
      shoulders_[side] = aim_pose_transform(shoulders_[side], targets[side + 2],
                                            {torso.at(1, 0), torso.at(1, 1), torso.at(1, 2)});
    }
  }
  for (std::size_t slot = 0; slot < 4; ++slot) {
    auto &worlds = muzzles_[slot];
    worlds.clear();
    if (lost[slot]) {
      continue;
    }
    for (const WeaponSocket &socket : sockets_[slot]) {
      if (slot < 2) {
        const auto &arm = pose_[slot + 1];
        if (socket.bone >= 0 && socket.bone < bone_slots && arm.set[socket.bone]) {
          worlds.push_back(socket.local * arm.bones[socket.bone]);
        }
      } else {
        Mat4 muzzle = socket.model * shoulders_[slot - 2];
        // 0x098bb7 preserves the barrel's position, re-aiming only its basis.
        if (!destroyed && (aim_modes[slot] == 0 || aim_modes[slot] == 1)) {
          muzzle = aim_pose_transform(muzzle, targets[slot],
                                      {muzzle.at(1, 0), muzzle.at(1, 1), muzzle.at(1, 2)});
        }
        worlds.push_back(muzzle);
      }
    }
  }
}

// 0x0889bf rolls the wheels each tick by the robot's ground speed (a tick's
// travel over pi), backwards in the back-off motions; a robot standing or
// turning on the spot rolls them by a fifth of the steering stick, and one
// in the air by the forward stick.
void RobotCombat::advance_wheels(const RobotMover &mover, const float seconds) {
  if (const auto &wheels = mover.tuning().wheels; wheels && mover.grounded()) {
    wheel_turn_ = std::remainder(wheel_turn_ + mover.local_velocity().z * seconds / wheels->radius,
                                 2.0F * pi);
    return;
  }
  constexpr float still_speed = 0.01F;   // 0x199f78
  constexpr float steer_roll = 0.2F;     // per tick, of the steering stick
  constexpr float max_roll = 3.1677728F; // radians a tick
  float speed = mover.stick()[1];
  if (mover.grounded()) {
    const Vec3 velocity = mover.local_velocity();
    speed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z) / battle_tick_rate;
    const int motion = mover.motion();
    if (motion == robot_motion::walk_back || dash_family(motion) == 1) {
      speed = -speed;
    }
    const bool turning = motion == robot_motion::turn_right || motion == robot_motion::turn_left;
    if (speed < still_speed || turning) {
      speed += std::abs(mover.stick()[0] * steer_roll);
    }
  }
  const float roll = std::clamp(speed / pi, -max_roll, max_roll);
  wheel_turn_ = std::remainder(wheel_turn_ + roll * seconds * battle_tick_rate, 2.0F * pi);
}

void RobotCombat::advance_auxiliary(const RobotMover &mover, const std::array<bool, 4> &firing,
                                    const bool piloted, const float seconds) {
  const float frames = seconds * tuning_.auxiliary_rate;
  const auto finish = [&](float value) {
    if (value >= tuning_.auxiliary_last) {
      return 0.0F;
    }
    return std::clamp(value + (value <= tuning_.auxiliary_release_turn ? -frames : frames), 0.0F,
                      tuning_.auxiliary_last);
  };
  // 0x091814: dash-body cycle; leg type 20 keeps it running continuously.
  if (mover.mode() == MoveMode::dash || assembly_.leg_type == 20) {
    auxiliary_[0] += frames;
    while (auxiliary_[0] > tuning_.auxiliary_loop_end) {
      auxiliary_[0] -= tuning_.auxiliary_loop_length;
    }
  } else {
    auxiliary_[0] = finish(auxiliary_[0]);
  }
  // 0x0916e9: held arm triggers open and hold the body channel; release
  // runs the rest of its clip and returns to the bind frame.
  if (firing[0] || firing[1]) {
    auxiliary_[1] = std::min(tuning_.auxiliary_release_turn,
                             auxiliary_[1] + frames * tuning_.auxiliary_fire_rate);
  } else if (auxiliary_[1] > 0) {
    auxiliary_[1] += frames * tuning_.auxiliary_fire_rate;
    if (auxiliary_[1] >= tuning_.auxiliary_last) {
      auxiliary_[1] = 0;
    }
  }
  // 0x091509: jump/air body channels hold at frame 44. The original
  // player's release resets immediately; AI runs its return animation.
  if (mover.mode() == MoveMode::jump) {
    auxiliary_[2] = std::min(tuning_.auxiliary_loop_end, auxiliary_[2] + frames);
  } else {
    auxiliary_[2] = piloted ? 0.0F : finish(auxiliary_[2]);
  }
}

std::optional<Mat4> RobotCombat::muzzle(const std::size_t slot) const {
  if (slot >= muzzles_.size() || muzzles_[slot].empty()) {
    return std::nullopt;
  }
  return muzzles_[slot][barrel_[slot] % muzzles_[slot].size()];
}

void RobotCombat::advance_muzzle(const std::size_t slot) {
  if (slot < muzzles_.size() && !muzzles_[slot].empty()) {
    barrel_[slot] = (barrel_[slot] + 1) % muzzles_[slot].size();
  }
}

bool RobotCombat::ready(const std::size_t slot) const {
  return slot >= 2 || weights_[slot] >= 1.0F - 1e-5F;
}
} // namespace phantomcrash
