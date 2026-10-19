#include "phantomcrash/robot.hpp"
#include "phantomcrash/robot_mover.hpp"

#include <cmath>

namespace phantomcrash {
namespace {
constexpr float dodge_frames_per_second = 60.0F; // robot +0x120 counts 2 frames a tick
} // namespace

// 0x0774ad, the Standard controls' double flick: past the threshold to
// one side (states 1 and 3), back within it (2 and 4) and past it to that
// side again dodges there. Each step has `flick_seconds`; then it starts over.
int RobotMover::flick(const float stick, const float seconds) {
  const float push = tuning_.flick_stick;
  const int side = stick > push ? 1 : stick < -push ? -1 : 0;
  const auto step = [this](const int state) {
    flick_state_ = state;
    flick_left_ = tuning_.flick_seconds;
  };
  int dodge = 0;
  switch (flick_state_) {
  case 0:
    if (side != 0) {
      step(side > 0 ? 1 : 3);
    }
    break;
  case 1:
  case 3: {
    const int held = flick_state_ == 1 ? 1 : -1;
    if (side == 0) {
      step(flick_state_ + 1);
    } else if (side != held) {
      step(side > 0 ? 1 : 3);
    }
    break;
  }
  default: {
    const int held = flick_state_ == 2 ? 1 : -1;
    if (side == held) {
      dodge = held;
      step(0);
    } else if (side != 0) {
      step(side > 0 ? 1 : 3);
    }
    break;
  }
  }
  flick_left_ -= seconds;
  if (flick_left_ < 0.0F) {
    flick_state_ = 0;
    flick_left_ = tuning_.flick_seconds;
  }
  return dodge;
}

// 0x08d98c: the robot's speed along the ground, in its own frame, plus the
// legs' push to the side it dodges to. The motion's frames time the rest.
void RobotMover::start_dodge(const int side) {
  dodged_ = side;
  const bool left = side < 0;
  const int motion = grounded_
                         ? (left ? robot_motion::dodge_left : robot_motion::dodge_right)
                         : (left ? robot_motion::air_dodge_left : robot_motion::air_dodge_right);
  const float push = grounded_ ? tuning_.dodge_speed : tuning_.air_dodge_speed;
  dodge_velocity_ = {local_velocity_.x + push * static_cast<float>(side), 0.0F, local_velocity_.z};
  dodge_slide_frames_ = 0.0F;
  dodge_ease_frames_ = dodge_last_frames_ = tuning_.dodge_seconds * dodge_frames_per_second;
  if (timings_ != nullptr && motion < static_cast<int>(timings_->size())) {
    const MotionTiming &timing = (*timings_)[static_cast<std::size_t>(motion)];
    if (timing.last > 0.0F) {
      dodge_slide_frames_ = timing.loop_start;
      dodge_ease_frames_ = timing.loop_end;
      dodge_last_frames_ = timing.last;
    }
  }
  set_mode(MoveMode::dodge, motion, false);
}

// 0x08ef79, the dodge's three phases; false once the motion has played.
bool RobotMover::tick_dodge(const float seconds) {
  const float frames = mode_seconds_ * dodge_frames_per_second;
  const float ticks = seconds * battle_tick_rate;
  if (frames < dodge_slide_frames_) {
    return true; // the robot goes on as it moved
  }
  if (frames < dodge_ease_frames_) {
    dodge_velocity_ = dodge_velocity_ * std::pow(tuning_.dodge_keep, ticks);
    local_velocity_.x = dodge_velocity_.x;
    local_velocity_.z = dodge_velocity_.z;
    return true;
  }
  if (frames >= dodge_last_frames_) {
    return false;
  }
  if (grounded_) {
    const float ease = (dodge_last_frames_ - frames) * tuning_.dodge_ease + tuning_.dodge_ease;
    local_velocity_.x = dodge_velocity_.x * ease;
    local_velocity_.z = dodge_velocity_.z * ease;
  } else {
    const float keep = std::pow(tuning_.air_dodge_keep, ticks);
    local_velocity_.x *= keep;
    local_velocity_.z *= keep;
  }
  return true;
}

} // namespace phantomcrash
