#include "desktop_online_factory.hpp"
#include "desktop_stage_objects.hpp"
#include <stdexcept>

namespace phantomcrash {
void join_online_battle(CoopMatch &match, const int slot, const online::BattlePlayer &player) {
  if (slot < 0 || slot >= online::max_players || match.players[slot].actor >= 0 ||
      player.profile.day != match.day || player.entry.area != match.area ||
      player.entry.contest != match.contest || !match.resources) {
    throw std::runtime_error("Online participant does not belong to this arena");
  }
  auto definition = std::static_pointer_cast<OnlineBattleDefinition>(match.resources);
  auto &data = *definition->resources;
  auto &battle = *match.battle;
  definition->setup.players[slot] = player;
  const auto *contest = data.contests.contest(match.contest);
  const auto stage_name = data.live.stages[match.area][contest && contest->night ? 1 : 0];
  const auto &stage = data.stages.at(stage_name);
  auto &script = definition->scripts[slot];
  script = rumbling_training(data.training, data.drive, data.rumbling, player.entry,
                             player.profile.garage, *data.parts, *data.garage);
  script.stage = stage_name;
  auto &tuning = script.battle.scenes;
  tuning.ranker =
      ranker_contest(definition->rules, match.area, match.contest) != RankerContest::none;
  tuning.ranker_gate = player.entry.ranker_gate;
  tuning.gate = player.entry.gate + slot;
  apply_stage(tuning, stage);
  if (stage.spawn) {
    script.battle.player_start = *stage.spawn;
  }
  prepare_online_player(match, slot, player, script, data);
  match.settlements[slot].reset();
  match.players[slot].victory_hold =
      !player.profile.cleared && match.contest == definition->rules.first_ranker
          ? static_cast<float>(
                data.roots.document("story/progression.json").number_or("clear_hold_frames", 0)) /
                part_frames_per_second
          : 0;
  static_cast<void>(load_stage_objects(&data.world, data.roots, tuning.stage, battle));
  static_cast<void>(load_stage_doors(&data.world, data.roots, tuning, battle));
  auto supply = script.battle.supply;
  if (stage.supply_pickups) {
    supply.look.pickups = stage.supply_pickups;
  }
  if (stage.supply_routes) {
    supply.routes = *stage.supply_routes;
    supply.stage_routes = true;
  }
  static_cast<void>(load_supply(&data.world, data.roots, tuning, std::move(supply), battle));
  const auto scene_file = data.roots.find_asset(tuning.tables);
  if (!scene_file) {
    throw std::runtime_error("Online battle scene table is missing");
  }
  const auto scene_tables = parse_battle_scenes(json::read_file(*scene_file));
  if (!match.entrances) {
    auto npc = tuning;
    npc.entrance = npc.exit = false;
    match.entrances = std::make_unique<BattleScene>(scene_tables, npc);
  }
  auto rider = tuning;
  rider.computer_entrance = rider.computer_exit = false;
  auto scene = std::make_shared<BattleScene>(scene_tables, rider, definition->setup.seed + slot);
  static_cast<void>(scene->start_entrance(battle));
  match.rider_scenes[slot] = std::move(scene);
  if (match.network_prepared) {
    data.assets.prepare(match);
  }
}
} // namespace phantomcrash
