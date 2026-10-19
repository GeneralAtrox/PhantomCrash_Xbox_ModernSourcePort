#include "phantomcrash/battle.hpp"
#include "phantomcrash/robot_combat.hpp"

#include <cmath>

namespace phantomcrash {
namespace {

std::optional<std::array<float, 2>> ndc_of(const Vec3 p, const Mat4 &m) {
  const float w = p.x * m.at(0, 3) + p.y * m.at(1, 3) + p.z * m.at(2, 3) + m.at(3, 3);
  if (w <= 0.0F) {
    return std::nullopt;
  }
  const Vec3 clip = transform_point(p, m);
  return std::array<float, 2>{clip.x / w, clip.y / w};
}

// The point as far from the eye as `guess` that the view shows at `ndc`,
// found from `guess` by a few Newton steps over the view's projection.
std::optional<Vec3> under_ndc(const TargetView &view, const Vec3 guess,
                              const std::array<float, 2> ndc) {
  const float distance = length(guess - view.eye);
  const float nudge = distance * 0.01F;
  if (nudge <= 0.0F) {
    return std::nullopt;
  }
  Vec3 at = guess;
  for (int step = 0; step < 4; ++step) {
    const Vec3 forward = normalize(at - view.eye);
    const Vec3 helper = std::abs(forward.y) < 0.99F ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    const Vec3 u = normalize(cross(helper, forward));
    const Vec3 v = cross(forward, u);
    const auto here = ndc_of(at, view.view_projection);
    const auto along_u = ndc_of(at + u * nudge, view.view_projection);
    const auto along_v = ndc_of(at + v * nudge, view.view_projection);
    if (!here || !along_u || !along_v) {
      return std::nullopt;
    }
    const float a = ((*along_u)[0] - (*here)[0]) / nudge;
    const float b = ((*along_v)[0] - (*here)[0]) / nudge;
    const float c = ((*along_u)[1] - (*here)[1]) / nudge;
    const float d = ((*along_v)[1] - (*here)[1]) / nudge;
    const float det = a * d - b * c;
    if (std::abs(det) < 1e-12F) {
      return std::nullopt;
    }
    const float x = ndc[0] - (*here)[0];
    const float y = ndc[1] - (*here)[1];
    const Vec3 moved = at + u * ((d * x - b * y) / det) + v * ((a * y - c * x) / det);
    at = view.eye + normalize(moved - view.eye) * distance;
  }
  return at;
}

} // namespace

// 0x098bb7 aims the shoulder weapons of aim modes 0 and 1 at the sight's
// own points, whatever they lock: mode 0 at the crosshair (robot +0x2688),
// mode 1 at the reticle as it tracks onto a target (+0x261c, written with
// the crosshair by 0x04f6a0 from the sight and its tracked offset). Only a
// piloted robot's drawn sight tracks; a computer robot's mode 1 shoulder
// keeps to its lock, else the crosshair.
std::optional<Vec3> Battle::tracked_sight(const Combatant &robot, const Vec3 crosshair) const {
  const auto found = target_views_.find(static_cast<int>(&robot - robots_.data()));
  if (!robot.piloted || found == target_views_.end() || !found->second || !found->second->boxes) {
    return std::nullopt;
  }
  return under_ndc(*found->second, crosshair, found->second->boxes->centre);
}

void Battle::pose_robot(const int index, const float seconds) {
  Combatant &robot = robots_[static_cast<std::size_t>(index)];
  robot.pose_updated = true;
  if (!robot.pose) {
    return;
  }
  std::array<Vec3, weapon_slots> targets;
  std::array<int, weapon_slots> aim_modes;
  const Vec3 crosshair = robot.aim_origin() + aim(robot) * robot.pose->aim_distance();
  const std::optional<Vec3> tracked = tracked_sight(robot, crosshair);
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    aim_modes[slot] = robot.weapons[slot] ? robot.weapons[slot]->aim_mode : -1;
    targets[slot] = crosshair;
    const int lock = robot.locks[slot];
    if (lock >= 0 && lock < static_cast<int>(robots_.size()) && robots_[lock].alive &&
        robot.weapons[slot] && robot.weapons[slot]->kind != WeaponKind::missile) {
      targets[slot] = robots_[lock].centre(settings_.robot_height);
    }
    if (slot >= static_cast<std::size_t>(WeaponSlot::right_shoulder)) {
      if (aim_modes[slot] == 0) {
        targets[slot] = crosshair;
      } else if (aim_modes[slot] == 1 && tracked) {
        targets[slot] = *tracked;
      }
    }
  }
  robot.pose->tick(robot.mover, robot.firing, robot.lost, targets, seconds, aim_modes,
                   robot.piloted);
}

Vec3 Battle::muzzle_direction(const Combatant &robot, const WeaponSlot slot) const {
  const auto *weapon = robot.weapons[static_cast<std::size_t>(slot)];
  if (weapon && weapon->kind == WeaponKind::missile) {
    // Shoulder sockets can point upward in their rest pose. Keep their
    // muzzle position, but launch rockets along the robot's aiming direction.
    // Armory converges this direction onto a valid lock at launch.
    return aim(robot);
  }
  if (robot.pose) {
    if (const auto matrix = robot.pose->muzzle(static_cast<std::size_t>(slot))) {
      return normalize(Vec3{-matrix->at(2, 0), -matrix->at(2, 1), -matrix->at(2, 2)});
    }
  }
  return aim(robot);
}

bool Battle::muzzle_blocked(const Combatant &robot, const WeaponSlot slot) const {
  if (!robot.pose || !robot.pose->muzzle(static_cast<std::size_t>(slot))) {
    return false;
  }
  const Vec3 from = robot.aim_origin(), to = muzzle(robot, slot);
  if (stage_.segment_hit(from, to)) {
    return true;
  }
  return doors_ && doors_->segment_hit(from, to).has_value();
}
} // namespace phantomcrash
