#include "desktop_online_factory.hpp"
#include <algorithm>

namespace phantomcrash {
void finish_online_battle(CoopMatch &match, const int slot) {
  const auto &participant = match.players.at(slot);
  if (participant.actor < 0 || !match.resources || match.settlements[slot]) {
    throw std::runtime_error("Online departure has no unsettled participant");
  }
  const auto definition = std::static_pointer_cast<OnlineBattleDefinition>(match.resources);
  const auto &script = definition->scripts[slot];
  const auto &robot = match.battle->robot(participant.actor);
  CoopMatch::Settlement settlement;
  auto &summary = settlement.summary;
  summary.outcome =
      participant.finished.value_or(robot.alive ? MatchOutcome::leave : MatchOutcome::retire);
  summary.shootdowns = robot.kills;
  summary.play_seconds = participant.play_seconds;
  summary.lost = robot.lost;
  summary.armour = robot.max_hp > 0 ? std::clamp(robot.hp / robot.max_hp, 0.0F, 1.0F) : 0;
  summary.robot.parts[static_cast<int>(PartSlot::body)] =
      RobotPart{script.robots.at("player").body, script.battle.player.body_level};
  summary.robot.parts[static_cast<int>(PartSlot::legs)] =
      RobotPart{script.robots.at("player").legs, script.battle.player.legs_level};
  for (std::size_t weapon = 0; weapon < weapon_slots; ++weapon) {
    if (const auto *tuning = robot.weapons[weapon]; tuning && tuning->part) {
      summary.robot.parts[static_cast<int>(PartSlot::right_arm) + weapon] =
          RobotPart{tuning->part->index, tuning->part->level};
    }
  }
  summary.rental = definition->setup.players[slot]->entry.rental;
  settlement.bounty = participant.bounty;
  settlement.beaten = participant.beaten;
  settlement.won = participant.won;
  match.settlements[slot] = std::move(settlement);
  // The profile copy is session-owned. Only the local receipt transition writes
  // the user's campaign; every replica releases the same actor exactly once.
  auto &profile = definition->setup.players[slot]->profile;
  match.battle->select_player(participant.actor);
  match.battle->set_player_target_view(std::nullopt);
  static_cast<void>(match.leave(slot, profile));
  match.rider_scenes[slot].reset();
  if (match.empty()) {
    static_cast<void>(match.world->give_back(match.simulation_save));
  }
}
} // namespace phantomcrash
