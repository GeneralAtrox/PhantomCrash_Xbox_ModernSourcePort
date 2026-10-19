// Battle: who is in it. Drones come in at the enemy path's entries and go
// back out; the live RUMBLING (rumbling_world.hpp) also arms them, changes
// who they are and brings them in itself.
#include "phantomcrash/battle.hpp"
#include "phantomcrash/robot_combat.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace phantomcrash {

int Battle::add_drone(const std::string &name, const float hp) {
  const int index = add_robot(name, 1, {}, 0.0F, {}, hp);
  Combatant &drone = robots_[static_cast<std::size_t>(index)];
  drone.drone = true;
  drone.rival = true;
  drone.frozen = !drones_active_;
  respawn(drone);
  return index;
}

void Battle::respawn(Combatant &drone) {
  drone.ai.reset();
  drone.camouflaged = false;
  drone.camouflage = 1.0F;
  if (drone.pose) {
    drone.pose->reset();
  }
  drone.alive = true;
  drone.dying = false;
  drone.death_seconds = 0.0F;
  drone.hp = drone.max_hp;
  drone.damage_from.clear();
  drone.respawn_left = 0.0F;
  drone.lost = {};
  drone.push = {};
  drone.last_ground.reset();
  drone.invulnerable = 0.0F;
  drone.shield = 0.0F;
  drone.shield_glow = 0.0F;
  const std::vector<int> entries = path_.entries();
  if (entries.empty()) {
    drone.mover.place({}, 0.0F);
    return;
  }
  // Take the entry farthest from the player, so drones do not appear on top of it.
  int best = entries.front();
  float best_distance = -1.0F;
  const Vec3 player_at = robots_.front().mover.position();
  for (const int entry : entries) {
    const Vec3 start = path_.segments[static_cast<std::size_t>(entry)].points.front();
    bool taken = false;
    for (const Combatant &other : robots_) {
      taken = taken || (&other != &drone && other.alive &&
                        length(other.mover.position() - start) < settings_.robot_radius * 3.0F);
    }
    const float distance = length(start - player_at) + (taken ? -1000.0F : 0.0F);
    if (distance > best_distance) {
      best_distance = distance;
      best = entry;
    }
  }
  const auto &segment = path_.segments[static_cast<std::size_t>(best)];
  const Vec3 start = segment.points.front();
  const Vec3 onward = segment.points.size() > 1 ? segment.points[1] : Vec3{};
  drone.mover.place(placed(start, drone.radius, drone.mover), heading_toward(onward - start));
  drone.path.start(path_, best);
  events_.push_back({static_cast<int>(&drone - robots_.data()), BattleEventKind::respawned});
}

void Battle::join_path(const int index) {
  Combatant &drone = robots_.at(static_cast<std::size_t>(index));
  const std::vector<int> entries = path_.entries();
  if (entries.empty()) {
    return;
  }
  const Vec3 at = drone.mover.position();
  const auto distance = [&](const int entry) {
    return length(path_.segments[static_cast<std::size_t>(entry)].points.front() - at);
  };
  // The entrance has already walked away from the gate. Resuming at point
  // zero would send the robot back inside and immediately trigger its exit.
  drone.path.join(path_, *std::ranges::min_element(entries, {}, distance), at);
}

void Battle::raise_shield(const int index) {
  raise_shield(index, settings_.entry_shield.seconds);
}

void Battle::raise_shield(const int index, const float seconds) {
  robots_.at(static_cast<std::size_t>(index)).shield = seconds;
}

const WeaponTuning &Battle::add_weapon(WeaponTuning tuning) {
  const std::string name = tuning.name;
  WeaponTuning &stored = settings_.weapons[name];
  stored = std::move(tuning);
  return stored;
}

void Battle::arm(const int index, const std::array<std::string, weapon_slots> &weapons) {
  Combatant &robot = robots_.at(static_cast<std::size_t>(index));
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    robot.weapons[slot] = nullptr;
    robot.magazines[slot] = {};
    robot.cooldown[slot] = 0.0F;
    robot.queued_rounds[slot] = 0;
    robot.locks[slot] = robot.lock_candidates[slot] = -1;
    robot.lock_held[slot] = 0.0F;
    if (weapons[slot].empty()) {
      continue;
    }
    const auto found = settings_.weapons.find(weapons[slot]);
    if (found == settings_.weapons.end()) {
      throw std::runtime_error("robot " + robot.name + ": no weapon \"" + weapons[slot] + "\"");
    }
    robot.weapons[slot] = &found->second;
    // Melee weapons need no ammunition.
    const bool melee = found->second.hud_kind == hud_kind::melee;
    robot.magazines[slot].reset(melee ? 0 : found->second.max_ammo, found->second.recharge_seconds);
  }
  robot.lost = {};
}

void Battle::set_identity(const int index, const std::string &name, const int team,
                          const float hp) {
  Combatant &robot = robots_.at(static_cast<std::size_t>(index));
  robot.ai.reset();
  robot.name = name;
  if (cooperative_) {
    // The mover borrows these from the pose. A replacement must not retain
    // a freed animation table until its simulation-owned pose is installed.
    robot.mover.set_timings(nullptr);
    robot.pose.reset();
  }
  robot.team = team;
  robot.hp = robot.max_hp = hp;
  robot.kills = 0;
  robot.damage_from.clear();
}

void Battle::set_auto_respawn(const int index, const bool automatic) {
  robots_.at(static_cast<std::size_t>(index)).auto_respawn = automatic;
}

void Battle::bring_in(const int index) {
  Combatant &robot = robots_.at(static_cast<std::size_t>(index));
  robot.mover.release();
  respawn(robot);
  if (std::ranges::find(arrivals_, index) == arrivals_.end()) {
    arrivals_.push_back(index);
  }
}

std::vector<int> Battle::take_arrivals() {
  return std::exchange(arrivals_, {});
}

void Battle::take_out(const int index) {
  robots_.at(static_cast<std::size_t>(index)).ai.reset();
  std::erase(arrivals_, index);
  Combatant &robot = robots_.at(static_cast<std::size_t>(index));
  robot.mover.release();
  robot.alive = false;
  robot.dying = false;
  robot.camouflaged = false;
  robot.push = {};
  robot.respawn_left = settings_.drone_respawn_seconds;
}

} // namespace phantomcrash
