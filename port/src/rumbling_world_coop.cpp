#include "phantomcrash/rumbling_world.hpp"
#include <algorithm>

namespace phantomcrash {
std::int64_t RumblingWorld::coop_bounty(const QuestSave &quest, const int entrant) const {
  return roster_card(check_, tables_.contests, quest, entrant, true).bounty;
}

void RumblingWorld::set_coop_level_offset(const int area, const int offset,
                                          const QuestSave &quest) {
  saved_battles_.fill({});
  auto &place = areas_.at(area);
  const int bounded = std::clamp(offset, -99, 99);
  if (place.enemy_level_offset == bounded) {
    return;
  }
  place.enemy_level_offset = bounded;
  for (std::size_t index = 0; index < place.roster.members.size(); ++index) {
    auto &actor = place.battle->robot(static_cast<int>(index) + 1);
    if (actor.piloted) {
      continue;
    }
    auto previous = actor;
    fit(area, index, quest);
    // Keep ongoing simulation state, including damage, lost weapons and ammo.
    previous.hp = previous.max_hp > 0 ? previous.hp / previous.max_hp * actor.max_hp : actor.hp;
    previous.max_hp = actor.max_hp;
    previous.combat = actor.combat;
    previous.lock_seconds = actor.lock_seconds;
    previous.sight = actor.sight;
    previous.weapons = actor.weapons;
    previous.mover.set_tuning(actor.mover.tuning());
    for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
      auto &magazine = previous.magazines[slot];
      magazine.max = actor.magazines[slot].max;
      magazine.recharge_seconds = actor.magazines[slot].recharge_seconds;
      magazine.ammo = std::min(magazine.ammo, magazine.max);
    }
    actor = std::move(previous);
  }
}

void RumblingWorld::release_coop_robot(const int area, const int actor) {
  saved_battles_.fill({});
  auto &place = areas_.at(area);
  place.battle->take_out(actor);
  place.battle->set_player_control(actor, {}, {});
  if (actor > 0 && actor <= static_cast<int>(place.roster.members.size())) {
    place.battle->robot(actor).piloted = false;
    place.battle->robot(actor).drone = true;
    auto &member = place.roster.members.at(actor - 1);
    member.live.down = true;
    member.live.arrival = 0;
    for (auto &other : place.roster.members) {
      if (other.live.arrival == waiting_for_opponent) {
        other.live.arrival = 0;
      }
    }
  }
}
} // namespace phantomcrash
