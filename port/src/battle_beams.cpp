// Beams (0x09b9fe): one ray held on the muzzle while the trigger is. Its
// tip runs out at the weapon's speed to its range. A robot the tip reaches
// is hit and the ray starts again from the muzzle, so a nearer robot is hit
// more often; walls and shut doors take an impact and the ray flies on
// (0x098eda cases 1 and 5). Let go, the beam fades out where it was.
#include "phantomcrash/battle_beam_trace.hpp"
#include "phantomcrash/battle_weapons.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/stage_collision.hpp"
#include "phantomcrash/stage_doors.hpp"

#include <algorithm>

namespace phantomcrash {
namespace {

// 0x17beec: the beam counts up to 8 frames held (60 Hz), then shows fully
// white; let go, it counts back down and is released at zero.
constexpr float beam_fade_seconds = 8.0F / 60.0F;
constexpr float beam_step_seconds = 1.0F / battle_ticks_per_second;
constexpr float beam_step_slack = 1e-4F; // float error in summed frame times

} // namespace

void Armory::hold_beam(const WeaponTuning &weapon, const int owner, const int slot, const int team,
                       const Vec3 muzzle, const Vec3 aim) {
  auto beam = std::ranges::find_if(beams_, [&](const Beam &held) {
    return held.owner == owner && held.slot == slot && held.weapon == &weapon;
  });
  if (beam == beams_.end()) {
    beams_.push_back({&weapon, owner, slot, team});
    beam = beams_.end() - 1;
  }
  beam->origin = muzzle;
  beam->direction = aim;
  beam->holding = true;
}

bool Armory::beam_live(const int owner, const int slot) const {
  return std::ranges::any_of(
      beams_, [&](const Beam &beam) { return beam.owner == owner && beam.slot == slot; });
}

void Armory::tick_beams(const float seconds, const std::vector<Target> &targets,
                        const StageCollision &stage, std::vector<WeaponHit> &hits,
                        const StageDoors *doors) {
  for (Beam &beam : beams_) {
    const WeaponTuning &weapon = *beam.weapon;
    if (!beam.holding) {
      beam.held -= seconds;
      beam.tip.reset();
      beam.step_left = 0.0F;
      continue;
    }
    beam.held = std::min(beam.held + seconds, beam_fade_seconds);
    // The tip steps once a battle tick (30 Hz), so it reaches a robot, and
    // hits it, no more often than the original's.
    beam.step_left -= seconds;
    if (beam.step_left > beam_step_slack) {
      continue;
    }
    beam.step_left = std::max(beam.step_left + beam_step_seconds, 0.0F);
    // The tip moves like a shot, from where it was to where it is now.
    const Vec3 from = beam.origin;
    const Vec3 before = beam.tip.value_or(from);
    beam.length = std::min(beam.length + weapon.speed * beam_step_seconds, weapon.range);
    const Vec3 tip = from + beam.direction * beam.length;
    beam.reach = beam.length;
    beam.tip = tip;
    Projectile shot;
    shot.weapon = &weapon;
    shot.owner = beam.owner;
    shot.slot = beam.slot;
    shot.team = beam.team;
    // 0x098eda case 1: a robot the tip reaches takes the hit and the ray is
    // gone; 0x09b9fe starts it again from the muzzle the next tick.
    int victim = -1;
    std::optional<Vec3> contact;
    float nearest = 0.0F;
    for (std::size_t index = 0; index < targets.size(); ++index) {
      const Target &body = targets[index];
      if (!body.alive || body.team == beam.team) {
        continue;
      }
      const auto point = body.hit_point(before, tip, 0.0F);
      if (point && (!contact || length(*point - before) < nearest)) {
        contact = point;
        nearest = length(*point - before);
        victim = static_cast<int>(index);
      }
    }
    if (beam.owner == 0 && beam_trace_enabled()) {
      trace_beam_step(beam, from, tip, targets, victim, contact);
    }
    // Case 5: a wall or a shut door leaf on the way takes the impact, and
    // the ray flies on through it.
    auto surface = stage.segment_contact(before, contact.value_or(tip));
    std::optional<Vec3> wall = surface ? std::optional<Vec3>{surface->position} : std::nullopt;
    if (const auto leaf = doors != nullptr
                              ? doors->segment_hit(before, wall.value_or(contact.value_or(tip)))
                              : std::nullopt) {
      wall = leaf;
      surface.reset();
    }
    if (wall) {
      detonate(shot, *wall, targets, hits, false, surface);
    }
    if (victim < 0) {
      continue;
    }
    const Target &body = targets[static_cast<std::size_t>(victim)];
    hits.push_back({victim, beam.owner, weapon.damage, weapon.stagger_seconds, *contact, &weapon,
                    beam.direction, true});
    const std::size_t first = hits.size();
    detonate(shot, *contact, targets, hits, body.on_robot, std::nullopt, victim);
    for (std::size_t index = first; index < hits.size(); ++index) {
      hits[index].direct = hits[index].direct || hits[index].victim == victim;
    }
    beam.reach = length(*contact - from);
    beam.length = 0.0F;
    beam.tip.reset();
  }
  for (Beam &beam : beams_) {
    beam.holding = false;
  }
  std::erase_if(beams_, [](const Beam &beam) { return beam.held <= 0.0F; });
}

} // namespace phantomcrash
