#include "phantomcrash/battle.hpp"
#include "phantomcrash/weapon_reach.hpp"
#include "target_view.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace phantomcrash {
namespace {

// The sight: 64 units ahead of the aim origin (0x04e6b8), and the canvas
// the chip's boxes are sized in.
constexpr float sight_distance = 64.0F;
constexpr float sight_canvas_width = 640.0F;
constexpr float sight_canvas_height = 480.0F;
constexpr float sight_near_plane = 0.3F;
constexpr float sight_far_plane = 6000.0F;

// 0x0525b0: whether `point` lands inside the box of half size `half`
// around the reticle's centre.
bool in_lock_box(const TargetView &view, const Vec3 point, const std::array<float, 2> centre,
                 const std::array<float, 2> half) {
  const Mat4 &m = view.view_projection;
  const Vec3 clip = transform_point(point, m);
  const float w = point.x * m.at(0, 3) + point.y * m.at(1, 3) + point.z * m.at(2, 3) + m.at(3, 3);
  return w > 0.0F && std::abs(clip.x / w - centre[0]) < half[0] &&
         std::abs(clip.y / w - centre[1]) < half[1];
}

} // namespace

bool in_target_view(const TargetView &view, const Vec3 point) {
  const Mat4 &m = view.view_projection;
  const Vec3 clip = transform_point(point, m);
  const float w = point.x * m.at(0, 3) + point.y * m.at(1, 3) + point.z * m.at(2, 3) + m.at(3, 3);
  // D3D's homogeneous clip volume, including near/far planes.
  return w > 0.0F && clip.z >= 0.0F && clip.z <= w && std::abs(clip.x) <= w &&
         std::abs(clip.y) <= w;
}

bool Battle::clear_target_sight(const Vec3 from, const Vec3 to) const {
  if (stage_.segment_hit(from, to) || wrecks_.segment_hit(from, to) ||
      (doors_ && doors_->segment_hit(from, to))) {
    return false;
  }
  return !objects_ || !std::ranges::any_of(objects_->objects(), [&](const StageObject &object) {
    return object.solid() && object.collision.segment_hit(from, to).has_value();
  });
}

bool Battle::target_visible(const Combatant &observer, const Combatant &target) const {
  const bool detected_cloak = observer.drone && settings_.ai.original && observer.ai.visible &&
                              observer.ai.target >= 0 &&
                              observer.ai.target < static_cast<int>(robots_.size()) &&
                              &robots_[static_cast<std::size_t>(observer.ai.target)] == &target;
  if (&observer == &target || !target.alive || target.team == observer.team ||
      (target.camouflaged && !detected_cloak) || target.mover.mode() == MoveMode::scripted) {
    return false;
  }
  const Vec3 to = target.centre(settings_.robot_height);
  if (dot(to - observer.aim_origin(), aim(observer)) <= 0.0F) {
    return false;
  }
  const auto view = target_views_.find(static_cast<int>(&observer - robots_.data()));
  if (observer.piloted && view != target_views_.end() && view->second) {
    if (!in_target_view(*view->second, to) || !clear_target_sight(view->second->eye, to)) {
      return false;
    }
  }
  return clear_target_sight(observer.aim_origin(), to);
}

int Battle::find_lock(const Combatant &robot, const WeaponTuning &weapon, const std::size_t slot,
                      const int held) const {
  const Vec3 from = robot.aim_origin();
  const Vec3 facing = aim(robot);
  // 0x51e37: the computer's robots lock a melee weapon from nearer.
  const float range = weapon.kind == WeaponKind::melee && !robot.piloted
                          ? std::min(weapon.lock_range, computer_melee_lock_range)
                          : weapon.lock_range;
  const auto bearing = [&](const Combatant &other, float &angle) {
    const Vec3 offset = other.centre(settings_.robot_height) - from;
    const float distance = length(offset);
    if (distance > range || distance < 1e-3F) {
      return false;
    }
    angle = std::acos(std::clamp(dot(normalize(offset), facing), -1.0F, 1.0F));
    return true;
  };
  const std::optional<TargetView> view = sight_view(robot);
  const bool outer = weapon.kind == WeaponKind::melee ||
                     slot >= static_cast<std::size_t>(WeaponSlot::right_shoulder);
  const auto inside_chip = [&](const Combatant &other) {
    return view && view->boxes &&
           in_lock_box(*view, other.centre(settings_.robot_height), view->boxes->centre,
                       outer ? view->boxes->outer_half : view->boxes->inner_half);
  };
  // Preserve the held target's priority, but homing acquisition and retention
  // must both respect the chip's box. The angular hold alone can keep a
  // candidate outside a narrow chip's bounds and even finish locking there.
  if (weapon.hold_degrees > 0.0F && held >= 0 && held < static_cast<int>(robots_.size())) {
    float angle = 0.0F;
    const Combatant &other = robots_[static_cast<std::size_t>(held)];
    if (bearing(other, angle) && angle <= radians(weapon.hold_degrees) &&
        target_visible(robot, other) && (!weapon.homing || inside_chip(other))) {
      return held;
    }
  }
  // 0x04f6a0, 0x05262d, 0x051e37: arm weapons lock inside the inner box,
  // shoulder and melee weapons inside the outer one, the nearest first.
  if (!view || !view->boxes) {
    return -1;
  }
  int nearest = -1;
  float nearest_distance = range;
  for (std::size_t index = 0; index < robots_.size(); ++index) {
    // The original's computer robots lock only what their brain targets.
    if (robot.drone && settings_.ai.original &&
        (!robot.ai.visible || robot.ai.target != static_cast<int>(index))) {
      continue;
    }
    const Combatant &other = robots_[index];
    const Vec3 centre = other.centre(settings_.robot_height);
    const float distance = length(centre - from);
    if (distance < nearest_distance && inside_chip(other) && target_visible(robot, other)) {
      nearest_distance = distance;
      nearest = static_cast<int>(index);
    }
  }
  return nearest;
}

// 0x04f6a0: the robot a robot locks (+0x130) among its rivals nearest
// first (0x04f152's list): within lock_range, not camouflaged, in its
// view with nothing between them, and inside its sight's inner box.
int Battle::ai_lock_target(const Combatant &drone) const {
  const std::optional<TargetView> view = sight_view(drone);
  if (!view || !view->boxes || !settings_.ai.original) {
    return -1;
  }
  const Vec3 from = drone.aim_origin();
  int nearest = -1;
  float nearest_distance = settings_.ai.original->value("lock_range");
  for (std::size_t index = 0; index < robots_.size(); ++index) {
    const Combatant &other = robots_[index];
    if (&other == &drone || !other.alive || other.team == drone.team || other.camouflaged ||
        other.mover.mode() == MoveMode::scripted) {
      continue;
    }
    const Vec3 centre = other.centre(settings_.robot_height);
    const float distance = length(centre - from);
    if (distance < nearest_distance && in_target_view(*view, centre) &&
        in_lock_box(*view, centre, view->boxes->centre, view->boxes->inner_half) &&
        clear_target_sight(from, centre)) {
      nearest_distance = distance;
      nearest = static_cast<int>(index);
    }
  }
  return nearest;
}

// 0x04e6b8 builds every robot's sight each tick, the drawn reticle or not:
// the point 64 units ahead of its aim origin along its heading plus twist
// and its look's pitch, seen through its camera, with its chip's boxes
// around it. The player's reticle, which tracks off the sight, gives its
// own boxes; a robot without one (a computer robot, a split screen, a fixed
// view) takes them around the sight, through its camera when it has one
// and from its own eyes otherwise.
std::optional<TargetView> Battle::sight_view(const Combatant &robot) const {
  std::optional<TargetView> view;
  if (const auto found = target_views_.find(static_cast<int>(&robot - robots_.data()));
      robot.piloted && found != target_views_.end() && found->second) {
    if (found->second->boxes) {
      return found->second;
    }
    view = found->second;
  }
  const Vec3 eye = robot.aim_origin();
  const Vec3 facing = aim(robot);
  if (!view) {
    const Mat4 projection =
        perspective(radians(settings_.sight_fov_degrees), sight_canvas_width / sight_canvas_height,
                    sight_near_plane, sight_far_plane);
    view = TargetView{look_at(eye, eye + facing, {0.0F, 1.0F, 0.0F}) * projection, eye, {}};
  }
  const Vec3 sight = eye + facing * sight_distance;
  const Mat4 &m = view->view_projection;
  const Vec3 clip = transform_point(sight, m);
  const float w = sight.x * m.at(0, 3) + sight.y * m.at(1, 3) + sight.z * m.at(2, 3) + m.at(3, 3);
  if (w <= 0.0F) {
    return std::nullopt; // the sight is behind the camera
  }
  const auto ndc = [](const std::array<float, 2> half) {
    return std::array<float, 2>{2.0F * half[0] / sight_canvas_width,
                                2.0F * half[1] / sight_canvas_height};
  };
  view->boxes =
      LockBoxes{{clip.x / w, clip.y / w}, ndc(robot.sight.inner_half), ndc(robot.sight.outer_half)};
  return view;
}

} // namespace phantomcrash
