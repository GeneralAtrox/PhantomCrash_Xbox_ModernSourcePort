#include "phantomcrash/battle.hpp"
#include "phantomcrash/money.hpp"

namespace phantomcrash {
void Battle::add_wreck_targets(std::vector<Target> &out) const {
  for (const auto &wreck : wrecks_.items()) {
    out.push_back({wreck.centre, wreck.radius, -1, wreck.solid(),
                   wreck.collision ? &*wreck.collision : nullptr, false});
  }
}

bool Battle::hit_wreck(const WeaponHit &hit) {
  const int supplies =
      supply_ && supply_->active() ? 1 + static_cast<int>(supply_->pickups().size()) : 0;
  const int index = hit.victim - static_cast<int>(robots_.size()) - object_count() - supplies;
  if (index < 0 || index >= static_cast<int>(wrecks_.items().size())) {
    return false;
  }
  const auto slot = static_cast<std::size_t>(index);
  // A projectile must strike the wreck itself. Nearby blasts and melee
  // strikes leave it intact, so its bounty still requires an aimed shot.
  const bool projectile = hit.weapon && hit.weapon->kind != WeaponKind::melee;
  const bool direct = hit.directional || hit.direct;
  if (projectile && direct && hit.damage > 0 && wrecks_.destroy(slot)) {
    BattleEvent event{hit.attacker, BattleEventKind::wreck_destroyed};
    event.at = wrecks_.items()[slot].centre;
    event.slot = settings_.robot_wrecks.break_effect;
    // Event money uses stored units (tens of NY), like supply and match prizes.
    // This bonus never touches kills, BINGO, or the downed robot's damage ledger.
    if (hit.attacker >= 0 && hit.attacker < static_cast<int>(robots_.size())) {
      event.amount = static_cast<float>(settings_.robot_wrecks.reward_ny / ny_per_stored_unit);
    }
    events_.push_back(event);
  }
  return true;
}
} // namespace phantomcrash
