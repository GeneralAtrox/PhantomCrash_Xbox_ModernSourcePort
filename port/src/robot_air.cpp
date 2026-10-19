#include "phantomcrash/robot_mover.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {

float held_jump_rise(const MovementTuning &tuning) {
  // tick_air's held lift, a battle tick at a time, until the rise stops.
  const JumpTakeoff takeoff = jump_takeoff(tuning.jump_height, tuning.gravity);
  const float seconds = 1.0F / battle_tick_rate;
  const float keep = 1.0F - jump_lift_ease;
  float speed = takeoff.speed;
  float lift_left = takeoff.lift_seconds;
  float rise = speed * seconds; // the take-off tick moves it already
  while (speed > 0.0F) {
    float lifted = 0.0F;
    if (lift_left > 0.0F) {
      speed = takeoff.speed + (speed - takeoff.speed) * keep;
      lifted = std::min(lift_left, seconds);
      lift_left -= lifted;
    }
    speed += tuning.gravity * (lifted - seconds);
    rise += std::max(speed, 0.0F) * seconds;
  }
  return rise;
}

void RobotMover::tick_air(const RobotControl &control, const float seconds) {
  if (mode_ == MoveMode::hit) {
    stagger_left_ -= seconds;
    lift_left_ = 0.0F;
    down_boosting_ = false;
    vertical_speed_ -= tuning_.gravity * seconds;
    if (stagger_left_ <= 0.0F) {
      set_mode(MoveMode::jump, robot_motion::air, true);
    }
    return;
  }
  // Re-pressing jump after release latches descent, as 0x0905af does,
  // once the lift spent outweighs the rise left (always while falling).
  // A separate mapping also permits the native dash-button shortcut.
  const bool can_descend = vertical_speed_ < tuning_.gravity * lift_spent_;
  if (!down_boosting_ && can_descend && (control.down_boost || (control.jump && !jump_was_held_))) {
    down_boosting_ = true;
    lift_left_ = 0.0F;
    set_mode(MoveMode::jump, robot_motion::air, true);
  }
  if (!down_boosting_ && control.dodge != 0 && mode_ != MoveMode::dodge) {
    start_dodge(control.dodge);
  } else if (mode_ == MoveMode::dodge && !tick_dodge(seconds)) {
    set_mode(MoveMode::jump, robot_motion::air, true);
  }
  // The held jump's lift (0x0905af): the rise eases toward the take-off
  // speed while gravity is cancelled, until the budget is spent. Letting go
  // ends it for this jump.
  float lifted = 0.0F;
  if (!down_boosting_ && control.jump && lift_left_ > 0.0F) {
    const float takeoff = jump_takeoff(tuning_.jump_height, tuning_.gravity).speed;
    const float keep = std::pow(1.0F - jump_lift_ease, seconds * battle_tick_rate);
    vertical_speed_ = takeoff + (vertical_speed_ - takeoff) * keep;
    lifted = std::min(lift_left_, seconds);
    lift_left_ -= lifted;
    lift_spent_ += lifted;
  }
  if (!control.jump) {
    lift_left_ = 0.0F;
  }
  vertical_speed_ += tuning_.gravity * lifted;
  vertical_speed_ -=
      (tuning_.gravity + (down_boosting_ ? tuning_.down_boost_acceleration : 0.0F)) * seconds;
  // Standard turns on the left stick in the air, or on the right one while
  // it is pushed, and does not strafe (0x0774ad, robot +0x2534).
  float steer_turn = control.move[0];
  float strafe = control.strafe;
  if (control.standard && !tuning_.standard_air_strafe) {
    steer_turn =
        std::abs(control.move[0]) > tuning_.standard_air_turn_stick ? control.move[0] : strafe;
    strafe = 0.0F;
  }
  turn(steer_turn, tuning_.air_turn_degrees_per_second, seconds);
  const float divisor = std::max(1.0F, std::hypot(strafe, control.move[1]));
  const float steer = control.move[1] / divisor * tuning_.walk_speed * tuning_.air_control;
  local_velocity_.z =
      std::clamp(local_velocity_.z + steer * seconds, -tuning_.dash_speed, tuning_.dash_speed);
  if (mode_ != MoveMode::dodge) {
    local_velocity_.x += strafe / divisor * tuning_.walk_speed * tuning_.air_control * seconds;
    const float speed = length(local_velocity_);
    if (speed > tuning_.dash_speed) {
      local_velocity_ = local_velocity_ * (tuning_.dash_speed / speed);
    }
  }
}

} // namespace phantomcrash
