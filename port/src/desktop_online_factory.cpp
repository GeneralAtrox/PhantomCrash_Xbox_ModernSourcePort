#include "desktop_online_factory.hpp"
#include "desktop_stage_objects.hpp"
#include <stdexcept>

namespace phantomcrash {

std::shared_ptr<CoopMatch> build_online_battle(std::shared_ptr<OnlineBattleResources> resources,
                                               const online::BattleSetup &setup) {
  auto definition = std::make_shared<OnlineBattleDefinition>();
  definition->resources = std::move(resources);
  // Host and clients all consume the same serialized genesis, including the
  // deliberate omission of process-local live_epoch from the save format.
  definition->setup = online::decode_battle_setup(online::encode_battle_setup(setup));
  auto &data = *definition->resources;
  const auto &agreed = definition->setup;
  definition->rules = data.rules;
  definition->rules.roster.live_size = agreed.robots;
  const RegulationCheck check{definition->rules, *data.parts, *data.garage, data.stats};
  const RumblingTables tables{*data.parts,   *data.garage,
                              data.contests, definition->rules,
                              data.drive,    data.training.battle.settings.weapons,
                              data.hud};
  auto settings = data.training.battle.settings;
  static_cast<void>(apply_weapon_records(settings.weapons, *data.parts));
  auto match = std::make_shared<CoopMatch>();
  match->resources = definition;
  match->day = agreed.world.day;
  match->area = agreed.area;
  match->contest = agreed.contest;
  match->options = agreed.options;
  match->simulation_save = agreed.world;
  match->world = std::make_shared<RumblingWorld>(check, tables, std::move(settings),
                                                 data.gameplay_stages, data.live, agreed.seed);
  match->battle = match->world->lend(agreed.area, match->simulation_save);
  if (!match->battle || match->simulation_save.rosters[agreed.area].contest != agreed.contest) {
    throw std::runtime_error("Online entry does not match the agreed day's contest");
  }
  auto &battle = *match->battle;
  battle.set_cooperative(true);
  match->world->set_coop_level_offset(agreed.area, agreed.options.enemy_level_offset,
                                      match->simulation_save);
  for (int slot = 0; slot < online::max_players; ++slot) {
    if (agreed.players[slot]) {
      join_online_battle(*match, slot, *agreed.players[slot]);
    }
  }
  match->world->prepare_deterministic();
  data.assets.prepare(*match);
  match->network_prepared = true;
  return match;
}

std::unique_ptr<TrainingScreen> make_online_battle_view(std::shared_ptr<CoopMatch> match,
                                                        const int slot, const GameOptions &options,
                                                        BattleSounds::PlaybackQuery playing,
                                                        const float victory_hold_seconds,
                                                        std::function<void()> exit) {
  if (!match || !match->network_prepared || slot < 0 || slot >= online::max_players ||
      match->players[slot].actor < 0) {
    throw std::runtime_error("Online view requires an initialized participant");
  }
  auto definition = std::static_pointer_cast<OnlineBattleDefinition>(match->resources);
  auto &data = *definition->resources;
  const auto &script = definition->scripts[slot];
  const auto &player = *definition->setup.players[slot];
  TrainingContext context{data.roots,
                          &data.world,
                          &data.robots,
                          script,
                          &data.stages.at(script.stage),
                          data.talk,
                          data.talk_tuning,
                          player.profile.player_name};
  context.standard_controls = options.controls.standard();
  context.beginner = options.controls.beginner != 0;
  context.controls = options.controls;
  context.day = player.profile.day;
  context.money = player.profile.money;
  context.live_battle = match->battle;
  context.live_world = match->world.get();
  context.live_area = match->area;
  context.match_results = true;
  context.coop_match = match;
  context.coop_slot = slot;
  context.sound_playing = std::move(playing);
  context.victory_hold_seconds = victory_hold_seconds;
  context.online_exit = std::move(exit);
  return std::make_unique<TrainingScreen>(data.textures, data.font, context, data.rumbling.lesson);
}
} // namespace phantomcrash
