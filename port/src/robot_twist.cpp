#include "phantomcrash/math3d.hpp"
#include "phantomcrash/robot_mover.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {

void RobotMover::tick_twist(const RobotControl &input, const float seconds) {
  // The mouse (the port's) turns no faster than the stick: the legs' turn
  // under the load (0x08e257), the dash's or the air's. What it asks for
  // beyond that turns over the next ticks, up to half a turn behind; moving
  // the mouse back drops what is left the other way, so the turn reverses
  // at once instead of first working it off.
  constexpr float mouse_backlog = pi;
  const float degrees_per_second = mode_ == MoveMode::dash ? tuning_.dash_turn_degrees_per_second
                                   : grounded_             ? tuning_.turn_degrees_per_second
                                                           : tuning_.air_turn_degrees_per_second;
  const float mouse_limit = radians(degrees_per_second) * seconds;
  if (input.mouse_aim[0] * mouse_yaw_ < 0.0F) {
    mouse_yaw_ = 0.0F;
  }
  mouse_yaw_ = std::clamp(mouse_yaw_ + input.mouse_aim[0], -mouse_backlog, mouse_backlog);
  const float mouse_step = std::clamp(mouse_yaw_, -mouse_limit, mouse_limit);
  mouse_yaw_ -= mouse_step;
  heading_ = wrap_angle(heading_ + mouse_step);
  // The quick turn takes the right stick; the upper body stays put.
  RobotControl control = input;
  if (control.quick_turn) {
    tick_quick_turn(control, seconds);
    control.twist = 0.0F;
  }
  if (control.reset_twist && twist_reset_left_ <= 0.0F) {
    twist_reset_from_ = twist_;
    twist_reset_left_ = tuning_.twist_reset_seconds;
  }
  twist_push_ = twist_reset_left_ > 0.0F ? 0.0F : control.twist;
  if (twist_reset_left_ > 0.0F) {
    twist_reset_left_ = std::max(twist_reset_left_ - seconds, 0.0F);
    twist_ = twist_reset_from_ * twist_reset_left_ / tuning_.twist_reset_seconds;
  } else {
    const float keep = std::pow(tuning_.twist_stick_keep, seconds * battle_tick_rate);
    twist_stick_ = twist_stick_ * keep + std::abs(control.twist) * (1.0F - keep);
    const float limit =
        radians(tuning_.twist_limit_degrees + tuning_.twist_stretch_degrees * twist_stick_);
    twist_ =
        std::clamp(twist_ + radians(tuning_.twist_degrees_per_second) * control.twist * seconds,
                   -limit, limit);
  }
  pitch_ =
      std::clamp(pitch_ + control.mouse_aim[1] + tuning_.pitch_per_second * control.pitch * seconds,
                 -tuning_.pitch_limit, tuning_.pitch_limit);
}

// 0x07d330's quick turn, Preferred with BEGINNER MODE (robot +0x273c). A
// push of the right stick past the threshold queues a quarter turn the way
// it is pushed (+0x2748), and later ticks take it at up to a share of the
// twist rate. Moving the left stick drops what is left; pushing the right
// stick the other way starts a cooldown (+0x2744) that drops it too and
// queues nothing until it runs out.
void RobotMover::tick_quick_turn(const RobotControl &control, const float seconds) {
  if (quick_turn_cooldown_ > 0.0F) {
    quick_turn_cooldown_ = std::max(quick_turn_cooldown_ - seconds, 0.0F);
    quick_turn_left_ = 0.0F;
    return;
  }
  const float push = control.twist;
  const float threshold = tuning_.quick_turn_stick;
  if (quick_turn_left_ == 0.0F) {
    if (push > threshold) {
      quick_turn_left_ = radians(tuning_.quick_turn_degrees);
    } else if (push < -threshold) {
      quick_turn_left_ = -radians(tuning_.quick_turn_degrees);
    }
    return;
  }
  if (std::abs(control.move[0]) > tuning_.stick_threshold) {
    quick_turn_left_ = 0.0F;
  }
  if (quick_turn_left_ > 0.0F ? push < -threshold : push > threshold) {
    quick_turn_cooldown_ = tuning_.quick_turn_cooldown_seconds;
  }
  const float most = radians(tuning_.twist_degrees_per_second) * tuning_.quick_turn_share * seconds;
  const float step = std::clamp(quick_turn_left_, -most, most);
  heading_ = wrap_angle(heading_ + step);
  quick_turn_left_ -= step;
}

} // namespace phantomcrash
