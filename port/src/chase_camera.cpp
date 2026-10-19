#include "phantomcrash/chase_camera.hpp"

#include "phantomcrash/robot_mover.hpp"
#include "phantomcrash/skeleton.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {

float number(const json::JsonValue &value, const char *key, const float fallback) {
  return static_cast<float>(value.number_or(key, fallback));
}

} // namespace

ChaseCameraSettings parse_chase_camera(const json::JsonValue &value) {
  ChaseCameraSettings s;
  s.fov_degrees = number(value, "fov_degrees", s.fov_degrees);
  s.near_plane = number(value, "near_plane", s.near_plane);
  s.far_plane = number(value, "far_plane", s.far_plane);
  s.tick_rate = number(value, "tick_rate", s.tick_rate);
  s.follow = number(value, "follow", s.follow);
  s.pitch_limit_degrees = number(value, "pitch_limit_degrees", s.pitch_limit_degrees);
  s.behind = number(value, "behind", s.behind);
  s.look_ahead = number(value, "look_ahead", s.look_ahead);
  s.height = number(value, "height", s.height);
  s.pitch_lift = number(value, "pitch_lift", s.pitch_lift);
  s.eye_follow_ticks = number(value, "eye_follow_ticks", s.eye_follow_ticks);
  s.wall_margin = number(value, "wall_margin", s.wall_margin);
  s.robot_clearance = number(value, "robot_clearance", s.robot_clearance);
  s.clearance_lift = number(value, "clearance_lift", s.clearance_lift);
  s.eye_radius = number(value, "eye_radius", s.eye_radius);
  s.robot_top = number(value, "robot_top", s.robot_top);
  return s;
}

void ChaseCamera::tick(const CameraSubject &subject, const StageCollision &stage,
                       const float seconds, const bool snap) {
  const float ticks = seconds * settings_.tick_rate;
  const float pitch_limit = radians(settings_.pitch_limit_degrees);
  const float pitch = std::clamp(subject.pitch, -pitch_limit, pitch_limit);
  if (snap) {
    yaw_ = subject.yaw;
    pitch_ = pitch;
  } else {
    // 0.3 of the way a tick, at any presentation rate.
    const float ease = 1.0F - std::pow(1.0F - settings_.follow, ticks);
    yaw_ = wrap_angle(yaw_ + wrap_angle(subject.yaw - yaw_) * ease);
    pitch_ += (pitch - pitch_) * ease;
  }
  // 0x07edfa: pitch, then turn; -z is ahead.
  const Mat4 aim = rotation_x(pitch_) * rotation_y(yaw_);
  const float lift = settings_.height + settings_.pitch_lift * std::fabs(pitch_);
  target_ = subject.position + transform_point({0.0F, lift, -settings_.look_ahead}, aim);
  Vec3 wanted = subject.torso + transform_point({0.0F, lift, settings_.behind}, aim);
  const float r = subject.height;
  const float top = settings_.robot_top;
  // In the air the eye rises to look down on the robot, and the sight
  // line starts one leg radius lower.
  if (!subject.grounded) {
    wanted.y += top - 0.5F * r;
  }
  anchor_ = subject.position + Vec3{0.0F, subject.grounded ? top : top - r, 0.0F};
  // 0x053db4: a wall on the sight line brings the eye off it.
  if (const auto hit = stage.segment_contact(anchor_, wanted)) {
    wanted = hit->position + hit->normal * settings_.wall_margin;
  }
  const Vec3 from = eye_;
  if (snap) {
    eye_ = wanted;
  } else {
    // A third of the gap a tick (0x0805a0(3.0)).
    const float keep = 1.0F - 1.0F / std::max(settings_.eye_follow_ticks, 1.0F);
    eye_ = eye_ + (wanted - eye_) * (1.0F - std::pow(keep, ticks));
  }
  // Too near the robot's lower sphere, the eye rises.
  const Vec3 centre = subject.position + Vec3{0.0F, r, 0.0F};
  if (length(eye_ - centre) < r + settings_.robot_clearance) {
    eye_.y += settings_.clearance_lift * ticks;
  }
  // 0x052c1f: the eye's sphere stops at the stage on its way.
  if (!snap) {
    if (const auto hit = stage.segment_contact(from, eye_)) {
      eye_ = hit->position + hit->normal * settings_.eye_radius;
    }
  }
  static_cast<void>(stage.push_sphere(eye_, settings_.eye_radius));
}

void ChaseCamera::block_eye(const Vec3 from, const Vec3 hit) {
  const Vec3 back = from - hit;
  eye_ = hit + normalize(back) * std::min(settings_.wall_margin, length(back));
}

Mat4 ChaseCamera::view() const {
  if (!shot_) {
    return look_at(eye_, target_, {0.0F, 1.0F, 0.0F});
  }
  // The roll turns the up vector about the line of sight.
  const Vec3 look = normalize(shot_->target - shot_->eye);
  const Vec3 side = normalize(cross(look, {0.0F, 1.0F, 0.0F}));
  const Vec3 up = cross(side, look);
  return look_at(shot_->eye, shot_->target,
                 up * std::cos(shot_->roll) + side * std::sin(shot_->roll));
}

Mat4 ChaseCamera::projection(const float aspect) const {
  const float fov = shot_ ? shot_->fov_degrees : settings_.fov_degrees;
  return perspective(radians(fov), aspect, settings_.near_plane, settings_.far_plane);
}

} // namespace phantomcrash
