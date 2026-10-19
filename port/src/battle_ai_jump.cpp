// The jump at a target off the sight: original 0x05949b, 0x059799,
// 0x05981c and 0x059921.
#include "phantomcrash/battle.hpp"
#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {
float stick(float error, float speed, float seconds) {
  return speed > 0 ? std::clamp(error / (speed * seconds), -1.0F, 1.0F) : 0;
}

// 0x06e331's zone 1, the high ground: outside the circle above one floor,
// inside it above another.
bool high_ground(const OriginalAiData &data, const Vec3 feet) {
  const float dx = feet.x - data.value("high_ground_centre_x");
  const float dz = feet.z - data.value("high_ground_centre_z");
  const bool outside = std::hypot(dx, dz) >= data.value("high_ground_radius");
  return outside ? feet.y > data.value("high_ground_outside_floor")
                 : feet.y >= data.value("high_ground_inside_floor");
}
} // namespace

void Battle::ai_jump(Combatant &drone, const float seconds) {
  auto &s = drone.ai;
  const auto &data = *settings_.ai.original;
  const auto &level = data.levels[static_cast<std::size_t>(s.ability)];
  const RobotMover &mover = drone.mover;
  const Combatant *target = s.target >= 0 && s.target < static_cast<int>(robots_.size())
                                ? &robots_[static_cast<std::size_t>(s.target)]
                                : nullptr;
  if (s.jump_phase == 0) {
    // 0x05949b (skipped in the special route, 0x05f47f): on the ground,
    // not chasing a box, no melee weapon (0x05ec15, kind 6), off the area
    // that skips it (quest stage index 2 and 3) and below area 1's high
    // ground, a target 5 to 20 units away more than 15 degrees off the
    // sight, at 15 in 255 a tick.
    if (target == nullptr || s.pickup >= 0 || s.mode == AiMode::route_special ||
        !mover.grounded() ||
        settings_.ai.area == static_cast<int>(data.value("jump_reaction_skipped_area")) ||
        (!settings_.ai.jump_any_floor && high_ground(data, mover.position()))) {
      return;
    }
    for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
      if (drone.weapons[slot] && !drone.lost[slot] &&
          drone.weapons[slot]->kind == WeaponKind::melee) {
        return;
      }
    }
    const Vec3 offset = target->mover.position() - mover.position();
    const float distance = length(offset);
    if (distance >= data.value("jump_reaction_far") ||
        distance <= data.value("jump_reaction_near")) {
      return;
    }
    const float off =
        std::acos(std::clamp(dot(normalize(aim(drone)), offset * (1 / distance)), -1.0F, 1.0F));
    if (off <= data.value("jump_reaction_angle") ||
        !ai_chance(random_, data.value("jump_reaction_chance"), data.value("probability_scale"))) {
      return;
    }
    // 0x059799 presses the jump with every stick let go.
    s.jump_phase = 1;
    s.control = {};
    s.control.jump = true;
    return;
  }
  // 0x05c1fb (flags 0xc) turns and tilts toward the held target where it
  // is, seen or not.
  const auto aim_at = [&](const Combatant &held) {
    const auto &motion = mover.tuning();
    const Vec3 offset = held.centre(settings_.robot_height) - drone.aim_origin();
    const float yaw = wrap_angle(heading_toward(offset) - mover.heading());
    const float pitch = std::atan2(offset.y, std::hypot(offset.x, offset.z));
    s.control.reset_twist = true;
    s.control.move[0] =
        stick(yaw, radians(motion.turn_degrees_per_second), seconds) * level.control_scale;
    s.control.pitch = stick(pitch - mover.pitch(), motion.pitch_per_second, seconds);
  };
  if (s.jump_phase < 3) {
    // 0x05981c: without a target, back to the route. Otherwise push forward
    // at full stick and aim, holding the jump through the air (motions 4
    // and 5); on the ground let go and aim for three seconds (0x17cce0 x
    // the tick rate).
    if (target == nullptr) {
      s.jump_phase = 0;
      ai_enter(s, AiMode::route, data, random_);
      return;
    }
    s.control = {};
    aim_at(*target);
    s.control.move[1] = level.control_scale;
    s.control.jump = true;
    if (!mover.grounded()) {
      s.jump_phase = 2;
    } else {
      s.control.jump = false;
      s.jump_phase = 3;
      s.jump_aim_left = data.value("jump_reaction_aim_seconds");
    }
    return;
  }
  // 0x059921: the forward push carries on (its stick is never reset); it
  // aims while it holds a target, until the time is up.
  if (target != nullptr) {
    aim_at(*target);
  }
  s.jump_aim_left -= seconds;
  if (s.jump_aim_left <= 0) {
    s.jump_phase = 0;
    ai_enter(s, AiMode::route, data, random_);
  }
}
} // namespace phantomcrash
