#include "desktop_battle_sights.hpp"

namespace phantomcrash {

void BattleSights::record_defeats(const Battle &battle) {
  for (const BattleEvent &event : battle.events()) {
    if (event.kind != BattleEventKind::destroyed || event.robot < 0 ||
        event.robot >= static_cast<int>(battle.robots().size())) {
      continue;
    }
    const auto &victim = battle.robots()[event.robot];
    // Every contributing attacker receives SHOOTDOWN credit, not only the last hit.
    if (viewer_ >= 0 && viewer_ < static_cast<int>(victim.damage_from.size()) &&
        victim.damage_from[viewer_] > 0) {
      defeated_.emplace(victim.name, victim.tag);
    }
  }
}

} // namespace phantomcrash
