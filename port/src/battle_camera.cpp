// The battle camera's views (0x07dd2a ticks the camera, 0x07ffa5 places it).
#include "phantomcrash/battle_camera.hpp"

#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/robot_mover.hpp"
#include "phantomcrash/skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

using json::JsonValue;

Vec3 vec3(const JsonValue &value, const char *name) {
  const auto v = json::numbers<3>(value, name);
  return {v[0], v[1], v[2]};
}

std::vector<float> floats(const JsonValue &value, const char *key, std::vector<float> fallback) {
  const JsonValue *list = value.find(key);
  if (list == nullptr) {
    return fallback;
  }
  std::vector<float> out;
  for (const JsonValue &number : list->array()) {
    out.push_back(static_cast<float>(number.number()));
  }
  return out;
}

float by_model(const std::vector<float> &table, const int model) {
  if (table.empty()) {
    return 0.0F;
  }
  return table[static_cast<std::size_t>(std::clamp(model, 0, static_cast<int>(table.size()) - 1))];
}

Vec3 lerp(const Vec3 a, const Vec3 b, const float t) {
  return a + (b - a) * t;
}

} // namespace

CameraViewSettings parse_camera_views(const JsonValue &value) {
  CameraViewSettings s;
  const auto number = [&](const char *key, const float fallback) {
    return static_cast<float>(value.number_or(key, fallback));
  };
  if (const JsonValue *fov = value.find("fov_degrees")) {
    const auto v = json::numbers<camera_view::count>(*fov, "fov_degrees");
    std::ranges::copy(v, s.fov_degrees.begin());
  }
  s.start_view = std::clamp(static_cast<int>(value.number_or("start_view", s.start_view)), 0,
                            camera_view::count - 1);
  s.tick_rate = number("tick_rate", s.tick_rate);
  s.blend_ticks = number("blend_ticks", s.blend_ticks);
  s.follow = number("follow", s.follow);
  s.pitch_limit_degrees = number("pitch_limit_degrees", s.pitch_limit_degrees);
  s.pitch_lift = number("pitch_lift", s.pitch_lift);
  if (const JsonValue *orbits = value.find("orbits")) {
    const auto &list = orbits->array();
    if (list.size() != s.orbits.size()) {
      throw std::runtime_error("camera_views: orbits needs three views");
    }
    for (std::size_t index = 0; index < list.size(); ++index) {
      s.orbits[index].offset = vec3(json::required(list[index], "offset"), "offset");
      s.orbits[index].pitch_lift = list[index].boolean_or("pitch_lift", false);
    }
  }
  s.eye_height = floats(value, "eye_height", s.eye_height);
  s.eye_forward = floats(value, "eye_forward", s.eye_forward);
  s.look_distance = number("look_distance", s.look_distance);
  s.fixed_pick_ticks = number("fixed_pick_ticks", s.fixed_pick_ticks);
  s.fixed_candidates = static_cast<int>(value.number_or("fixed_candidates", s.fixed_candidates));
  return s;
}

std::vector<Vec3> parse_fixed_cameras(const JsonValue &document, const int stage) {
  std::vector<Vec3> points;
  const JsonValue *stages = document.find("fixed_cameras");
  const JsonValue *entry = stages != nullptr ? stages->find(std::to_string(stage)) : nullptr;
  if (entry == nullptr) {
    return points;
  }
  for (const JsonValue &point : json::required(*entry, "points").array()) {
    points.push_back(vec3(point, "fixed camera"));
  }
  return points;
}

CameraViews::CameraViews(const CameraViewSettings &settings, std::vector<Vec3> fixed_points)
    : settings_(settings), fixed_points_(std::move(fixed_points)), view_(settings.start_view),
      from_(settings.start_view) {}

bool CameraViews::change(const ViewChange change) {
  if (blend_left_ > 0.0F) {
    return false;
  }
  int next = view_;
  switch (change) {
  case ViewChange::first_person:
    next = camera_view::first_person;
    break;
  case ViewChange::behind:
    next = camera_view::behind;
    break;
  case ViewChange::next: // 2 -> 3 -> 4 -> 5 -> 2
    next = view_ < camera_view::right_side ? camera_view::right_side : ((view_ - 1) & 3) + 2;
    break;
  case ViewChange::previous: // 2 -> 5 -> 4 -> 3 -> 2
    next = view_ < camera_view::right_side ? camera_view::fixed : ((view_ + 1) & 3) + 2;
    break;
  }
  if (next == view_) {
    return false;
  }
  const bool blends = change == ViewChange::first_person || change == ViewChange::behind;
  from_ = view_;
  view_ = next;
  blend_left_ = blends ? settings_.blend_ticks : 0.0F;
  if (view_ == camera_view::fixed) {
    pick_left_ = 0.0F;
  }
  return true;
}

void CameraViews::tick(const CameraSubject &subject, const CameraShot &chase, const float seconds,
                       const SightTest &sees, const bool snap) {
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
  if (view_ == camera_view::fixed || from_ == camera_view::fixed) {
    if (pick_left_ <= 0.0F || fixed_point_ < 0) {
      pick_fixed(subject, sees);
      pick_left_ = std::max(pick_left_ + settings_.fixed_pick_ticks, ticks);
    }
    pick_left_ -= ticks;
  }
  shot_ = place(view_, subject, chase);
  if (blend_left_ > 0.0F) {
    // 0x07fea8: the old view's share is the timer's.
    const float t = settings_.blend_ticks > 0.0F ? blend_left_ / settings_.blend_ticks : 0.0F;
    const CameraShot old = place(from_, subject, chase);
    shot_.eye = lerp(shot_.eye, old.eye, t);
    shot_.target = lerp(shot_.target, old.target, t);
    shot_.fov_degrees += (old.fov_degrees - shot_.fov_degrees) * t;
    blend_left_ = std::max(blend_left_ - ticks, 0.0F);
  }
  if (blend_left_ <= 0.0F) {
    from_ = view_;
  }
}

float CameraViews::first_person_share() const noexcept {
  const float t = settings_.blend_ticks > 0.0F ? blend_left_ / settings_.blend_ticks : 0.0F;
  if (view_ == camera_view::first_person) {
    return 1.0F - t;
  }
  return from_ == camera_view::first_person ? t : 0.0F;
}

CameraShot CameraViews::place(const int view, const CameraSubject &subject,
                              const CameraShot &chase) const {
  CameraShot shot;
  shot.fov_degrees = settings_.fov_degrees[static_cast<std::size_t>(view)];
  const Mat4 turn = rotation_y(yaw_);
  const Vec3 lift{0.0F, settings_.pitch_lift * std::fabs(pitch_), 0.0F};
  // Views 2-5 look at the torso, one leg radius up.
  const Vec3 torso_target = subject.torso + Vec3{0.0F, subject.height, 0.0F};
  switch (view) {
  case camera_view::first_person: {
    const Mat4 aim = rotation_x(pitch_) * turn;
    const Vec3 eye{0.0F, by_model(settings_.eye_height, subject.legs_model),
                   -by_model(settings_.eye_forward, subject.legs_model)};
    shot.eye = subject.torso + transform_point(eye, aim);
    shot.target = subject.position + Vec3{0.0F, subject.height, 0.0F} +
                  transform_point({0.0F, 0.0F, -settings_.look_distance}, aim);
    break;
  }
  case camera_view::right_side:
  case camera_view::left_side:
  case camera_view::front: {
    const OrbitView &orbit = settings_.orbits[static_cast<std::size_t>(view - 2)];
    const Vec3 offset = orbit.pitch_lift ? orbit.offset + lift : orbit.offset;
    shot.eye = subject.position + transform_point(offset, turn);
    shot.target = torso_target;
    break;
  }
  case camera_view::fixed:
    if (fixed_point_ >= 0 && fixed_point_ < static_cast<int>(fixed_points_.size())) {
      shot.eye = fixed_points_[static_cast<std::size_t>(fixed_point_)];
      shot.target = torso_target;
      break;
    }
    [[fallthrough]]; // a stage without fixed cameras shows the chase view
  default:
    shot.eye = chase.eye;
    shot.target = chase.target;
    break;
  }
  return shot;
}

void CameraViews::pick_fixed(const CameraSubject &subject, const SightTest &sees) {
  if (fixed_points_.empty()) {
    fixed_point_ = -1;
    return;
  }
  // The nearest few points, nearest first; the first that sees the robot,
  // or else the nearest.
  std::vector<int> order(fixed_points_.size());
  std::iota(order.begin(), order.end(), 0);
  const auto distance = [&](const int index) {
    return length(fixed_points_[static_cast<std::size_t>(index)] - subject.position);
  };
  std::ranges::stable_sort(order, {}, distance);
  order.resize(std::min(order.size(), static_cast<std::size_t>(settings_.fixed_candidates)));
  const Vec3 target = subject.position + Vec3{0.0F, subject.height, 0.0F};
  fixed_point_ = order.front();
  for (const int index : order) {
    if (!sees || sees(fixed_points_[static_cast<std::size_t>(index)], target)) {
      fixed_point_ = index;
      break;
    }
  }
}

} // namespace phantomcrash
