#include "phantomcrash/robot_mover.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {

constexpr float still_speed = 0.01F; // 0x199f78, as the walkers' wheels use it

} // namespace

void RobotMover::tick_wheels(const RobotControl &control, const float seconds) {
  const auto &wheels = *tuning_.wheels;
  const float throttle = std::clamp(control.move[1], -1.0F, 1.0F);
  // Wheels steer toward lateral stick input instead of translating sideways.
  const float steering = std::clamp(control.move[0] + control.strafe, -1.0F, 1.0F);
  const bool pushed = std::abs(throttle) > tuning_.stick_threshold;
  if (mode_ == MoveMode::dash && (!pushed || (throttle < 0) != backward_)) {
    set_mode(MoveMode::brake, robot_motion::brake, false);
  }
  if (pushed && mode_ != MoveMode::dash &&
      (control.dash || dash_hold_ >= tuning_.dash_hold_seconds)) {
    backward_ = throttle < 0;
    set_mode(MoveMode::dash, backward_ ? robot_motion::dash_back : robot_motion::dash, true);
  }
  dash_hold_ = std::abs(throttle) > tuning_.dash_stick ? dash_hold_ + seconds : 0;
  const bool boosting = mode_ == MoveMode::dash;
  const float forward_speed = boosting ? tuning_.dash_speed : tuning_.walk_speed;
  const float reverse_speed = boosting ? tuning_.dash_back_speed : tuning_.back_speed;
  const float target = throttle * (throttle < 0 ? reverse_speed : forward_speed);
  const bool braking = !pushed || target * local_velocity_.z < 0;
  const float accel = braking ? wheels.braking : wheels.acceleration;
  const float step = accel * seconds;
  local_velocity_.z += std::clamp(target - local_velocity_.z, -step, step);

  // Preserve world momentum while the chassis turns. Grip then gradually
  // removes sideways slip, producing drift and recovery instead of a snap.
  const Vec3 momentum = velocity();
  const float steer_scale = local_velocity_.z < 0 ? -wheels.reverse_turn_scale : wheels.turn_scale;
  turn(steering,
       (boosting ? tuning_.dash_turn_degrees_per_second : tuning_.turn_degrees_per_second) *
           steer_scale,
       seconds);
  local_velocity_.z = dot(momentum, heading_forward(heading_));
  local_velocity_.x =
      dot(momentum, heading_right(heading_)) * std::exp(-wheels.lateral_grip * seconds);
  if (boosting) {
    motion_speed_ = tuning_.dash_motion_rate;
  } else if (pushed) {
    backward_ = throttle < 0;
    set_mode(MoveMode::walk, backward_ ? robot_motion::walk_back : robot_motion::walk, true);
    motion_speed_ = std::abs(throttle) * tuning_.walk_motion_rate;
  } else if (std::abs(steering) > tuning_.stick_threshold) {
    set_mode(MoveMode::turn, steering > 0 ? robot_motion::turn_right : robot_motion::turn_left,
             true);
  } else if (length(local_velocity_) <= still_speed) {
    local_velocity_ = {};
    set_mode(MoveMode::stand, robot_motion::stand, true);
  } else {
    set_mode(MoveMode::brake, robot_motion::brake, false);
  }
}

} // namespace phantomcrash
