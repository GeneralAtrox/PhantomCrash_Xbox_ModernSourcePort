#include "phantomcrash/robot_mover.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {
constexpr int strike_left = 0x17; // 0x09dd24, the left arm's; the right arm's is one later
constexpr int strike_model_left = 0x19;
constexpr int air_strike_offset = 10;             // 0x09c337, while robot +0x252c is clear
constexpr float strike_frames_per_second = 60.0F; // robot +0x120 counts 2 frames a tick
} // namespace

int strike_motion(const int weapon_model, const int side, const bool grounded) {
  const int left = weapon_model == strike_motion_model ? strike_model_left : strike_left;
  return left + (side == 0 ? 0 : 1) + (grounded ? 0 : air_strike_offset);
}

bool RobotMover::strike(const StrikeStart &start) {
  if (striking() || mode_ == MoveMode::hit || mode_ == MoveMode::scripted ||
      mode_ == MoveMode::destroyed) {
    return false;
  }
  strike_armed_frames_ = start.armed_frames;
  strike_last_frames_ = start.last_frames;
  lunge_at_ = start.lunge_at;
  // 0x09c337: without a lock the target is the robot's own position.
  lunge_height_ = lunge_at_ ? lunge_at_->y : position_.y;
  lunge_piloted_ = start.piloted;
  down_boosting_ = false;
  lift_left_ = 0.0F;
  set_mode(MoveMode::strike, start.motion, false, true);
  return true;
}

void RobotMover::follow_lunge(const std::optional<Vec3> target) {
  if (lunge_at_) {
    lunge_at_ = target;
  }
}

bool RobotMover::blade_armed() const noexcept {
  return striking() && mode_seconds_ * strike_frames_per_second < strike_armed_frames_;
}

void RobotMover::tick_strike(const float seconds) {
  // 0x08fc1e: the strike ends at the motion's last frame, standing on the
  // ground or falling in the air.
  if (mode_seconds_ * strike_frames_per_second >= strike_last_frames_) {
    lunge_at_.reset();
    local_velocity_ = {};
    if (grounded_) {
      set_mode(MoveMode::stand, robot_motion::stand, true);
    } else {
      set_mode(MoveMode::jump, robot_motion::air, true);
    }
    return;
  }
  // Disarmed (phase 3) it stands where it is.
  local_velocity_ = {};
  const bool lunging = lunge_at_.has_value();
  if (blade_armed()) {
    if (lunging) {
      lunge(seconds);
      return;
    }
    local_velocity_.z = tuning_.strike_speed;
  }
  if (!grounded_) {
    vertical_speed_ -= tuning_.gravity * seconds;
  }
}

void RobotMover::lunge(const float seconds) {
  if (seconds <= 0.0F) {
    return;
  }
  const Vec3 offset = *lunge_at_ - position_;
  const float error = wrap_angle(heading_toward(offset) - heading_);
  if (std::abs(error) < radians(tuning_.lunge_cone_degrees)) {
    const float turn =
        radians(lunge_piloted_ ? tuning_.lunge_turn_degrees : tuning_.computer_lunge_turn_degrees) *
        seconds * battle_tick_rate;
    heading_ = wrap_angle(heading_ + std::clamp(error, -turn, turn));
    const float top =
        tuning_.dash_motion_rate *
        (lunge_piloted_ ? tuning_.lunge_rate_speed : tuning_.computer_lunge_rate_speed);
    local_velocity_.z = std::min(std::hypot(offset.x, offset.z) / seconds, top);
  }
  const float most = jump_takeoff(tuning_.jump_height, tuning_.gravity).speed;
  vertical_speed_ =
      std::clamp((lunge_height_ - position_.y) * tuning_.lunge_climb_rate, -most, most);
  if (vertical_speed_ > 0.0F) {
    grounded_ = false;
  }
}

} // namespace phantomcrash
