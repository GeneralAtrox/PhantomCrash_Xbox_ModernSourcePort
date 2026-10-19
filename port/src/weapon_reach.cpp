#include "phantomcrash/weapon_reach.hpp"

#include "phantomcrash/battle_weapons.hpp"
#include "phantomcrash/math3d.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {

// 0x09d42f: 9.8 / (rate / 2)^2 times the world's 2.5, a tick at 30 Hz.
constexpr float world_gravity = 9.8F; // 0x19f8a0
constexpr float world_scale = 2.5F;   // 0x19f89c
constexpr float lobbed_gravity =
    world_gravity / (battle_ticks_per_second * battle_ticks_per_second) * world_scale;

} // namespace

float straight_reach(const float start, const float top, const float acceleration,
                     const float life) {
  if (acceleration == 0.0F) {
    return start * life;
  }
  const float rise = (top - start) / acceleration; // frames to top speed
  if (rise >= life) {
    return 0.5F * acceleration * life * life + start * life;
  }
  return 0.5F * acceleration * rise * rise + start * rise + (life - rise) * top;
}

float lobbed_reach(const float speed, const float launch_degrees) {
  const float angle = radians(launch_degrees);
  if (std::sin(angle) < 0.0F) {
    return lobbed_reach_fallback;
  }
  const float up = std::sin(angle) * speed;
  const float along = std::cos(angle) * speed;
  return std::abs(2.0F * up * (along / lobbed_gravity));
}

WeaponReach weapon_reach(const WeaponStats &stats, const bool shoulder) {
  WeaponReach out;
  out.lock_range = shoulder ? shoulder_lock_range : arm_lock_range;
  out.hold_degrees = shoulder ? lock_hold_degrees : 0.0F;
  if (stats.hud_kind == hud_kind::melee) {
    out.range = melee_reach;
    out.lock_range = melee_lock_range;
    out.hold_degrees = lock_hold_degrees;
  } else if (stats.hud_kind == hud_kind::beam) {
    out.range = stats.top_speed; // the beam's full length
  } else if (stats.flight == weapon_flight_lobbed && shoulder) {
    const float reach = lobbed_reach(stats.start_speed, stats.launch_degrees);
    out.range = reach + stats.blast_radius + lobbed_reach_margin;
    out.min_range = std::max(reach - stats.blast_radius - lobbed_reach_margin, 0.0F);
  } else {
    out.range =
        straight_reach(stats.start_speed, stats.top_speed, stats.acceleration, stats.life_frames);
  }
  return out;
}

} // namespace phantomcrash
