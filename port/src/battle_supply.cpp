// The battle's supply drops: robots touching a pickup take it (0x0673db)
// and it gives them money, armour or rounds (0x066e50).
#include "phantomcrash/battle.hpp"
#include "phantomcrash/battle_chatter.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {

constexpr int supply_team = -1; // no side's: every shot can strike them

} // namespace

void Battle::set_supply(SupplySettings settings) {
  supply_.emplace(std::move(settings), random_());
}

void Battle::tick_supply(const float seconds) {
  if (!supply_ || !supply_->active()) {
    return;
  }
  // The rescue: players' robots low on armour call for a repair box.
  const float critical = supply_->settings().rescue.armour_share;
  std::vector<Vec3> robots;
  std::vector<int> rescue;
  robots.reserve(robots_.size());
  for (const Combatant &robot : robots_) {
    if (robot.piloted && robot.alive && robot.hp < robot.max_hp * critical) {
      rescue.push_back(static_cast<int>(robots.size()));
    }
    robots.push_back(robot.mover.position());
  }
  supply_->watch(std::move(robots), std::move(rescue));
  supply_->tick(
      seconds,
      [this](const Vec3 at) {
        const float height = stage_.ground_height(at, 0.0F); // the ground below it
        return height == no_ground ? 0.0F : height;
      },
      [this](const Vec3 from, const Vec3 to, const float radius) -> std::optional<Vec3> {
        if (const auto hit = stage_.segment_hit(from, to)) {
          return hit;
        }
        return stage_.distance_to(to) <= radius ? std::optional<Vec3>{to} : std::nullopt;
      });
  if (const auto crash = supply_->take_crash()) {
    supply_explosion(*crash);
  }
  if (supply_->take_special_arrival()) {
    BattleEvent arrival{-1, BattleEventKind::special_carrier};
    arrival.at = supply_->carrier().position;
    events_.push_back(arrival);
  }
  for (const Vec3 at : supply_->take_bursts()) {
    BattleEvent burst{-1, BattleEventKind::effect};
    burst.slot = supply_->settings().look.burst_effect;
    burst.at = at;
    events_.push_back(burst);
  }
  for (Combatant &robot : robots_) {
    if (!robot.alive) {
      continue;
    }
    // The robot's lower collision sphere (robot +0xb0, radius +0x24).
    if (const auto taken = supply_->take(robot.aim_origin(), robot.radius)) {
      take_supply(robot, *taken);
    }
  }
}

void Battle::supply_explosion(const Vec3 at) {
  const SupplyLook &look = supply_->settings().look;
  armory_.explode(at, look.crash_radius, look.crash_seconds);
  BattleEvent blast{-1, BattleEventKind::effect};
  blast.slot = look.crash_effect;
  blast.at = at;
  events_.push_back(blast);
}

// Shots see the carrier and the pickups (0x067603) with no side, after
// the robots.
void Battle::add_supply_targets(std::vector<Target> &out) const {
  if (!supply_ || !supply_->active()) {
    return;
  }
  const SupplySettings &settings = supply_->settings();
  out.push_back({supply_->carrier().position, supply_->carrier_radius(), supply_team,
                 supply_->carrier_shootable()});
  for (const SupplyPickup &pickup : supply_->pickups()) {
    const bool out_now =
        pickup.state == SupplyState::falling || pickup.state == SupplyState::landed;
    out.push_back({pickup.position, settings.pickup_radius, supply_team, out_now});
  }
}

// 0x047ed9: a shot that brings the carrier down pays its shooter, who
// remarks on it; one that strikes a pickup bursts it (a blast's reach
// does not).
bool Battle::hit_supply(const WeaponHit &hit) {
  const auto robots = static_cast<int>(robots_.size());
  if (hit.victim < robots) {
    return false;
  }
  if (!supply_) {
    return true;
  }
  const int index = hit.victim - robots - object_count();
  if (index > 0) {
    if (hit.directional) {
      supply_->shoot_pickup(static_cast<std::size_t>(index - 1));
    }
    return true;
  }
  if (!supply_->shoot_carrier()) {
    return true;
  }
  // 0x06781a starts group 0x3f immediately, before the wreck begins to fall.
  supply_explosion(supply_->carrier().position);
  if (hit.attacker >= 0 && hit.attacker < robots) {
    BattleEvent event{hit.attacker, BattleEventKind::carrier_down};
    event.amount = supply_->shot_bounty();
    events_.push_back(event);
    chatter(robots_[static_cast<std::size_t>(hit.attacker)], chatter_event::shot_carrier);
  }
  return true;
}

void Battle::take_supply(Combatant &robot, const SupplyTaken &taken) {
  const auto index = static_cast<int>(&robot - robots_.data());
  const SupplyKind &kind = taken.kind;
  BattleEvent event{index, BattleEventKind::supply};
  event.slot = taken.type;
  event.at = taken.at;
  switch (kind.effect) {
  case SupplyEffect::money:
    event.amount = kind.amount;
    break;
  case SupplyEffect::repair:
    // 0x06acc9: the share of the full armour, whole points.
    robot.hp = std::min(robot.hp + std::trunc(robot.max_hp * kind.amount), robot.max_hp);
    chatter(robot, robot.hp >= robot.max_hp * chatter_low_armour ? chatter_event::repaired
                                                                 : chatter_event::repaired_low);
    break;
  case SupplyEffect::ammo: {
    // 0x06ad29: one weapon among those short of rounds (the emptiest, or
    // as the original one at random) gets the share of its magazine,
    // rounded up; the remark goes by the share before it (robot +0x110).
    std::vector<std::size_t> short_slots;
    std::vector<float> shares;
    for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
      const Magazine &magazine = robot.magazines[slot];
      if (robot.weapons[slot] != nullptr && !robot.lost[slot] && magazine.limited() &&
          magazine.ammo < magazine.max) {
        short_slots.push_back(slot);
        shares.push_back(static_cast<float>(magazine.ammo) / static_cast<float>(magazine.max));
      }
    }
    if (!short_slots.empty()) {
      const bool emptiest = supply_->settings().ammo_emptiest;
      Magazine &magazine = robot.magazines[short_slots[pick_ammo_slot(shares, emptiest, random_)]];
      magazine.refill(static_cast<int>(std::ceil(static_cast<float>(magazine.max) * kind.amount)));
    }
    const float share = robot.ammo_share;
    if (share < chatter_half_armour) {
      chatter(robot, chatter_event::ammo_low);
    } else if (share < 1.0F) {
      chatter(robot, chatter_event::ammo);
    }
    break;
  }
  }
  events_.push_back(event);
}

} // namespace phantomcrash
