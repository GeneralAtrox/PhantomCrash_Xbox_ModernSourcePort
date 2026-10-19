#include "phantomcrash/robot_mover.hpp"
#include "phantomcrash/robot.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {

constexpr float motion_frames_a_second = 60.0F; // as RobotAnimator plays them

// How long a leg motion takes to play to its last frame at its own pace.
float motion_seconds(const std::vector<MotionTiming> *timings, const int motion) {
  if (timings == nullptr || motion < 0 || motion >= static_cast<int>(timings->size())) {
    return 0.0F;
  }
  return (*timings)[static_cast<std::size_t>(motion)].last / motion_frames_a_second;
}

// Moves `value` toward `target` by at most `step`.
float approach(const float value, const float target, const float step) {
  if (value < target) {
    return std::min(value + step, target);
  }
  return std::max(value - step, target);
}

} // namespace

JumpTakeoff jump_takeoff(const float height, const float gravity) {
  if (height <= 0.0F || gravity <= 0.0F) {
    return {};
  }
  // sqrt(2 g h) would throw the robot the whole height (0x075a36).
  const float throw_speed = std::sqrt(2.0F * gravity * height);
  JumpTakeoff out;
  out.speed = jump_takeoff_share * throw_speed + gravity / battle_tick_rate;
  out.lift_seconds = jump_lift_share * height / out.speed;
  return out;
}

Vec3 heading_forward(const float heading) {
  return {-std::sin(heading), 0.0F, -std::cos(heading)};
}

float heading_toward(const Vec3 direction) {
  return std::atan2(-direction.x, -direction.z);
}

Vec3 heading_right(const float heading) {
  return {-std::cos(heading), 0.0F, std::sin(heading)};
}

float wrap_angle(float angle) {
  angle = std::fmod(angle + pi, 2.0F * pi);
  if (angle < 0.0F) {
    angle += 2.0F * pi;
  }
  return angle - pi;
}

void RobotMover::place(const Vec3 position, const float heading) {
  position_ = position;
  heading_ = wrap_angle(heading);
  rolling_steer_ = 0;
  twist_ = mouse_yaw_ = 0.0F;
  twist_stick_ = 0.0F;
  twist_reset_left_ = 0.0F;
  quick_turn_left_ = quick_turn_cooldown_ = 0.0F;
  pitch_ = 0.0F;
  lunge_at_.reset();
  grounded_ = position.y <= floor_ + ground_snap;
  if (grounded_) {
    position_.y = std::max(position_.y, floor_);
  }
  local_velocity_ = {};
  coast_velocity_ = {};
  coast_brake_ = false;
  vertical_speed_ = 0.0F;
  down_boosting_ = false;
  jump_was_held_ = false;
  lift_left_ = 0.0F;
  set_mode(MoveMode::stand, robot_motion::stand, true, true);
}

Vec3 RobotMover::velocity() const {
  return heading_forward(heading_) * local_velocity_.z +
         heading_right(heading_) * local_velocity_.x + coast_velocity_ +
         Vec3{0.0F, vertical_speed_, 0.0F};
}

void RobotMover::set_mode(const MoveMode mode, const int motion, const bool loops,
                          const bool restart) {
  if (mode != mode_ || motion != motion_ || restart) {
    mode_seconds_ = 0.0F;
    ++motion_revision_;
  }
  mode_ = mode;
  motion_ = motion;
  loops_ = loops;
  motion_speed_ = 1.0F;
}

void RobotMover::stagger(const float seconds, const Vec3 push) {
  lunge_at_.reset(); // a hit breaks the strike off
  down_boosting_ = false;
  lift_left_ = 0.0F;
  // 0x07675e sets the hit's time (robot +0x25a0) to the stun, at most the
  // reaction's frames, and 0x08f989 stands the robot up once the motion
  // reaches it; every hit restarts the reaction.
  stagger_left_ = std::min(seconds, tuning_.hit_seconds);
  // The push in the robot's own frame picks the reaction: along or across,
  // whichever is larger, and its sign.
  const float along = dot(push, heading_forward(heading_));
  const float across = dot(push, heading_right(heading_));
  int motion = robot_motion::hit_left;
  if (std::abs(along) > std::abs(across)) {
    motion = along <= 0.0F ? robot_motion::hit_back : robot_motion::hit_forward;
  } else if (across > 0.0F) {
    motion = robot_motion::hit_right;
  }
  if (grounded_) {
    local_velocity_ = {};
  } else {
    motion += robot_motion::air_hit_offset;
  }
  set_mode(MoveMode::hit, motion, false, true);
}

void RobotMover::script(const ScriptedStep &step) {
  lunge_at_.reset();
  down_boosting_ = false;
  lift_left_ = 0.0F;
  if (mode_ != MoveMode::scripted || motion_ != step.motion || loops_ != step.loops) {
    set_mode(MoveMode::scripted, step.motion, step.loops);
  }
  heading_ = wrap_angle(step.heading);
  local_velocity_ = {dot(step.velocity, heading_right(heading_)), 0.0F,
                     dot(step.velocity, heading_forward(heading_))};
  motion_speed_ = step.motion_speed;
}

void RobotMover::release() {
  down_boosting_ = false;
  jump_was_held_ = false;
  lift_left_ = 0.0F;
  local_velocity_ = {};
  set_mode(MoveMode::stand, robot_motion::stand, true);
}

void RobotMover::destroy() {
  lunge_at_.reset();
  down_boosting_ = false;
  lift_left_ = 0.0F;
  local_velocity_ = {};
  vertical_speed_ = std::min(vertical_speed_, 0.0F);
  twist_ = pitch_ = 0.0F;
  motion_speed_ = 1.0F;
  set_mode(MoveMode::destroyed, robot_motion::death, false);
}

void RobotMover::turn(const float stick, const float degrees_per_second, const float seconds) {
  heading_ = wrap_angle(heading_ + radians(degrees_per_second) * stick * seconds);
}

void RobotMover::tick(const RobotControl &input, const float seconds,
                      const RootMotionSource &root) {
  RobotControl control = input;
  if (control.standard) {
    const int flicked = flick(control.strafe, seconds);
    control.dodge = control.dodge != 0 ? control.dodge : flicked;
  }
  mode_seconds_ += seconds;
  dodged_ = 0;
  strafing_ = false;
  if (mode_ == MoveMode::scripted || mode_ == MoveMode::destroyed) {
    if (!grounded_) {
      vertical_speed_ -= tuning_.gravity * seconds;
    }
  } else if (mode_ == MoveMode::strike) {
    tick_twist(control, seconds);
    tick_strike(seconds);
  } else {
    tick_twist(control, seconds);
    if (grounded_) {
      tick_ground(control, seconds, root);
    } else {
      tick_air(control, seconds);
    }
  }
  apply_root_stride(control, seconds, root);
  tick_root_coast(control, seconds);
  position_ = position_ + velocity() * seconds;
  if (grounded_ && position_.y > floor_ + ground_snap) {
    grounded_ = false; // walked off an edge
    if (mode_ != MoveMode::scripted && mode_ != MoveMode::destroyed && mode_ != MoveMode::strike) {
      set_mode(MoveMode::jump, robot_motion::jump, true);
    }
  }
  if (position_.y <= floor_ && vertical_speed_ <= 0.0F) {
    land(floor_);
  } else if (grounded_) {
    position_.y = floor_;
  }
  jump_was_held_ = control.jump;
  stick_ = control.move;
  rolling_steer_ =
      tuning_.wheels ? std::clamp(control.move[0] + control.strafe, -1.0F, 1.0F) : control.move[0];
}

void RobotMover::land(const float height) {
  position_.y = height;
  if (grounded_) {
    return;
  }
  grounded_ = true;
  down_boosting_ = false;
  lift_left_ = 0.0F;
  lift_spent_ = 0.0F;
  vertical_speed_ = 0.0F;
  if (mode_ == MoveMode::hit) {
    local_velocity_ = {}; // the rest of the reaction on the spot
  }
  if (mode_ == MoveMode::jump) {
    loops_ = false; // the jump motion plays its landing
    mode_seconds_ = 0.0F;
  }
}

bool RobotMover::jump_ready() const noexcept {
  const bool landing = mode_ == MoveMode::jump && !loops_ && mode_seconds_ < landing_seconds;
  const bool busy = mode_ == MoveMode::hit || mode_ == MoveMode::dodge ||
                    mode_ == MoveMode::strike || mode_ == MoveMode::scripted ||
                    mode_ == MoveMode::destroyed;
  return grounded_ && !jump_was_held_ && !landing && !busy;
}

void RobotMover::bump_head() {
  vertical_speed_ = std::min(vertical_speed_, 0.0F);
  lift_left_ = 0.0F;
}

void RobotMover::tick_ground(const RobotControl &control, const float seconds,
                             const RootMotionSource &root) {
  const float forward = control.move[1];
  const float side = control.move[0];
  const float strafe = std::abs(control.strafe) > tuning_.stick_threshold ? control.strafe : 0;
  const bool pushed = std::abs(forward) > tuning_.stick_threshold;
  const bool steering = std::abs(side) > tuning_.stick_threshold;

  if (mode_ == MoveMode::hit) {
    stagger_left_ -= seconds;
    if (stagger_left_ > 0.0F) {
      return;
    }
    set_mode(MoveMode::stand, robot_motion::stand, true);
  }
  if (mode_ == MoveMode::dodge) {
    turn(side, tuning_.turn_degrees_per_second, seconds); // 0x08ef79 steers it
    if (tick_dodge(seconds)) {
      return;
    }
    set_mode(MoveMode::stand, robot_motion::stand, true);
  }
  if (mode_ == MoveMode::jump && !loops_ && mode_seconds_ < landing_seconds) {
    local_velocity_ = local_velocity_ * std::max(0.0F, 1.0F - 8.0F * seconds);
    return; // landing
  }
  if (control.dodge != 0) {
    start_dodge(control.dodge);
    return;
  }
  if (control.jump && !jump_was_held_) {
    grounded_ = false;
    // 0x08ea5b: the take-off adds to any rise and fills the lift.
    const JumpTakeoff takeoff = jump_takeoff(tuning_.jump_height, tuning_.gravity);
    vertical_speed_ = std::max(vertical_speed_, 0.0F) + takeoff.speed;
    lift_left_ = takeoff.lift_seconds;
    lift_spent_ = 0.0F;
    set_mode(MoveMode::jump, robot_motion::jump, true, true);
    return;
  }

  if (tuning_.wheels) {
    tick_wheels(control, seconds);
    return;
  }

  if (coast_brake_) {
    coast_brake_ = false;
    set_mode(MoveMode::brake, robot_motion::brake, false);
  }
  if (stop_root_walk(control, root)) {
    turn(side, tuning_.turn_degrees_per_second, seconds);
    return;
  }

  // Dashing: a click of the stick, or holding it far over for 30 frames
  // (0x08ff1e); it lasts while the stick stays pushed.
  if (mode_ == MoveMode::dash) {
    turn(side, tuning_.dash_turn_degrees_per_second, seconds);
    if (std::abs(forward) < tuning_.dash_stick * 0.5F || (forward < 0.0F) != backward_) {
      if (tuning_.dash_stride && root.bank) {
        coast_velocity_ = velocity();
        coast_velocity_.y = 0.0F;
        local_velocity_ = {};
      }
      set_mode(MoveMode::brake, robot_motion::brake, false);
    } else {
      const float speed = backward_ ? -tuning_.dash_back_speed : tuning_.dash_speed;
      const float divisor = std::hypot(strafe, 1.0F);
      local_velocity_.z =
          approach(local_velocity_.z, speed / divisor, tuning_.walk_accel * 2.0F * seconds);
      local_velocity_.x = approach(local_velocity_.x, std::abs(speed) * strafe / divisor,
                                   tuning_.walk_accel * 2.0F * seconds);
      motion_speed_ = tuning_.dash_motion_rate;
      steer_dash(side, seconds);
      return;
    }
  }
  if (mode_ == MoveMode::brake) {
    local_velocity_.z = approach(local_velocity_.z, 0.0F, tuning_.walk_accel * 1.5F * seconds);
    local_velocity_.x = approach(local_velocity_.x, 0.0F, tuning_.walk_accel * 1.5F * seconds);
    // The stop plays through its hold and settle to the last frame before
    // the robot stands; the stick may cut it short once brake_cut_seconds pass.
    const bool steered = pushed || strafe != 0 || steering;
    if (mode_seconds_ < tuning_.brake_cut_seconds ||
        (!steered && mode_seconds_ < motion_seconds(timings_, robot_motion::brake))) {
      return;
    }
  }
  if (pushed && (control.dash || dash_hold_ >= tuning_.dash_hold_seconds)) {
    dash_hold_ = 0.0F;
    backward_ = forward < 0.0F;
    set_mode(MoveMode::dash, backward_ ? robot_motion::dash_back : robot_motion::dash, true);
    motion_speed_ = tuning_.dash_motion_rate;
    return;
  }
  dash_hold_ = std::abs(forward) > tuning_.dash_stick ? dash_hold_ + seconds : 0.0F;

  if (pushed || strafe != 0) {
    backward_ = forward < 0.0F;
    turn(side, tuning_.turn_degrees_per_second, seconds);
    const float full = backward_ ? tuning_.back_speed : tuning_.walk_speed;
    // The walk runs at half to full speed with the stick (0x08e6b6).
    const float magnitude = std::hypot(strafe, pushed ? forward : 0);
    const float amount = std::clamp(magnitude, 0.5F, 1.0F);
    const float target = (pushed ? forward : 0) * full * amount / magnitude;
    local_velocity_.z = approach(local_velocity_.z, target, tuning_.walk_accel * seconds);
    local_velocity_.x = approach(local_velocity_.x, strafe * full * amount / magnitude,
                                 tuning_.walk_accel * seconds);
    set_mode(MoveMode::walk, backward_ ? robot_motion::walk_back : robot_motion::walk, true);
    motion_speed_ = amount * tuning_.walk_motion_rate;
    strafing_ = !pushed;
    return;
  }
  local_velocity_.z = approach(local_velocity_.z, 0.0F, tuning_.walk_accel * seconds);
  local_velocity_.x = approach(local_velocity_.x, 0.0F, tuning_.walk_accel * seconds);
  if (steering) {
    // Sideways on the spot turns in place (the lesson at state 0x1a).
    turn(side, tuning_.turn_degrees_per_second, seconds);
    set_mode(MoveMode::turn, side > 0.0F ? robot_motion::turn_right : robot_motion::turn_left,
             true);
    return;
  }
  if (mode_ != MoveMode::stand && length(local_velocity_) < 0.5F) {
    set_mode(MoveMode::stand, robot_motion::stand, true);
  }
}

} // namespace phantomcrash
