#include "phantomcrash/root_motion.hpp"
#include "phantomcrash/motion.hpp"
#include "phantomcrash/robot.hpp"
#include "phantomcrash/robot_mover.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {
constexpr float root_frames_per_second = 60.0F;

int dash_family(const int motion) {
  return motion >= 2 && motion <= 4 ? 2 : motion >= 5 && motion <= 7 ? 5 : -1;
}

float start_frame(const RootMotionSource &source, const RobotMover &mover) {
  const int family = dash_family(mover.motion());
  const bool leaning =
      family >= 0 && family == dash_family(source.motion) && source.motion != mover.motion();
  return (source.motion == mover.motion() && source.revision == mover.motion_revision()) || leaning
             ? source.frame
             : 0.0F;
}
} // namespace

int root_motion_track(const SkeletonNode &node) {
  if (node.flags == node_root_motion && node.track >= 0) {
    return node.track;
  }
  for (const SkeletonNode &child : node.children) {
    if (const int track = root_motion_track(child); track >= 0) {
      return track;
    }
  }
  return -1;
}

std::optional<float> root_motion_step(const Motion &motion, const MotionTiming &timing, float from,
                                      float frames, const bool loop, const int track) {
  const auto position = [&](const float frame) { return motion.sample(track, frame).translation; };
  if (!position(from)) {
    return std::nullopt;
  }
  float moved = 0.0F;
  const float span = timing.loop_end - timing.loop_start;
  if (loop && span > 0.0F) {
    if (from >= timing.loop_end) {
      from = timing.loop_start + std::fmod(from - timing.loop_start, span);
    }
    if (from + frames > timing.loop_end) {
      moved += position(from)->z - position(timing.loop_end)->z;
      frames -= timing.loop_end - from;
      const float cycles = std::floor(frames / span);
      moved += cycles * (position(timing.loop_start)->z - position(timing.loop_end)->z);
      frames -= cycles * span;
      from = timing.loop_start;
    }
  }
  const float to = loop && span > 0.0F ? from + frames : std::min(from + frames, timing.last);
  return moved + position(from)->z - position(to)->z;
}

bool RobotMover::stop_root_walk(const RobotControl &control, const RootMotionSource &root) {
  if (mode_ != MoveMode::walk || !tuning_.walk_stride || !root.bank || !timings_ || motion_ < 0 ||
      motion_ >= static_cast<int>(timings_->size()) ||
      std::abs(control.move[1]) > tuning_.stick_threshold ||
      std::abs(control.strafe) > tuning_.stick_threshold ||
      (loops_ && std::abs(local_velocity_.z) <= 1e-5F)) {
    return false;
  }
  const Motion *clip = root.bank->motion(motion_);
  const MotionTiming &timing = (*timings_)[motion_];
  if (!clip || !root_motion_step(*clip, timing, root.frame, 0.0F, false, root.track)) {
    return false;
  }
  // 0x08ff1e jumps to the walk's last frame, ending it in 0x08e6b6.
  // 0x090d21 carries the previous world displacement with the leg's drag.
  coast_velocity_ = velocity();
  coast_velocity_.y = 0.0F;
  coast_brake_ = root.frame >= timing.loop_start;
  local_velocity_ = {};
  set_mode(MoveMode::stand, robot_motion::stand, true);
  return true;
}

void RobotMover::tick_root_coast(const RobotControl &control, const float seconds) {
  if (!grounded_ || tuning_.wheels || std::abs(control.strafe) > tuning_.stick_threshold ||
      mode_ == MoveMode::dodge || mode_ == MoveMode::hit || mode_ == MoveMode::strike ||
      mode_ == MoveMode::scripted || mode_ == MoveMode::destroyed ||
      length(coast_velocity_) < tuning_.stop_speed) {
    coast_velocity_ = {};
    coast_brake_ = false;
    return;
  }
  // 0x090e27 adds twice the normal steering while this slide remains.
  turn(control.move[0], 2.0F * tuning_.turn_degrees_per_second, seconds);
  coast_velocity_ = coast_velocity_ * std::pow(tuning_.stop_keep, seconds * battle_tick_rate);
}

void RobotMover::apply_root_stride(const RobotControl &control, const float seconds,
                                   const RootMotionSource &root) {
  const bool walking = mode_ == MoveMode::walk && tuning_.walk_stride;
  const bool dashing = (mode_ == MoveMode::dash || mode_ == MoveMode::brake) && tuning_.dash_stride;
  if ((!walking && !dashing) || !grounded_ || tuning_.wheels || !root.bank || !timings_ ||
      seconds <= 0.0F || motion_ < 0 || motion_ >= static_cast<int>(timings_->size())) {
    return;
  }
  // Pure lateral movement is the requested native control extension and
  // retains its configured acceleration. Forward/diagonal travel follows
  // the actual stride, normalized so diagonals gain no extra speed.
  if (walking && loops_ && std::abs(control.move[1]) <= tuning_.stick_threshold) {
    return;
  }
  const Motion *clip = root.bank->motion(motion_);
  if (!clip) {
    return;
  }
  const auto moved =
      root_motion_step(*clip, (*timings_)[motion_], start_frame(root, *this),
                       seconds * root_frames_per_second * motion_speed_, loops_, root.track);
  if (!moved) {
    return;
  }
  if (mode_ == MoveMode::brake) {
    local_velocity_ = {0, 0, *moved / seconds};
    return;
  }
  const float speed = *moved / seconds * (backward_ ? -1.0F : 1.0F);
  const float forward = dashing ? (backward_ ? -1.0F : 1.0F) : control.move[1];
  const float strafe = std::abs(control.strafe) > tuning_.stick_threshold ? control.strafe : 0.0F;
  const float magnitude = std::hypot(strafe, forward);
  if (magnitude > 0.0F) {
    local_velocity_ = {strafe * speed / magnitude, 0.0F, forward * speed / magnitude};
  }
}
} // namespace phantomcrash
