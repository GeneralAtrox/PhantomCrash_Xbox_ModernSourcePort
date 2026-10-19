#include "desktop_screens.hpp"
#include "desktop_coop_matches.hpp"
#include "desktop_game_screens.hpp"
#include "desktop_training.hpp"
#include "desktop_world_renderer.hpp"
#include "desktop_online_wait.hpp"

namespace phantomcrash {

std::unique_ptr<Screen> ScreenRunner::make_coop_battle(const RumblingEntry &entry) {
  if (coop_->online_game) {
    coop_->online_game->request_entry(quest_, entry);
    return std::make_unique<OnlineWaitScreen>(
        textures_, font_, coop_->online_game,
        CoopMatches::Key{quest_.day, entry.area, entry.contest}, coop_slot_);
  }
  if (!coop_->matches) {
    coop_->matches = std::make_shared<CoopMatches>();
  }
  const CoopMatches::Key key{quest_.day, entry.area, entry.contest};
  auto &match = coop_->matches->matches[key];
  if (!match) {
    // A previous day's world may still belong to another player. Its shared
    // owner stays alive while this player creates an independent day's arena.
    if (rumbling_world_ && rumbling_world_->lent() >= 0) {
      rumbling_world_.reset();
      rumbling_failed_ = false;
    }
    auto *world = rumbling_world();
    if (!world) {
      throw std::runtime_error("Co-op requires the live rumbling world");
    }
    match = std::make_shared<CoopMatch>();
    match->day = quest_.day;
    match->area = entry.area;
    match->contest = entry.contest;
    match->options = coop_->options;
    match->simulation_save = quest_;
    match->world = rumbling_world_;
    match->battle = world->lend(entry.area, match->simulation_save);
    if (!match->battle) {
      throw std::runtime_error("Co-op contest has no battle roster");
    }
    world->set_coop_level_offset(entry.area, match->options.enemy_level_offset,
                                 match->simulation_save);
    match->battle->set_cooperative(true);
  }
  const auto *contest = game_->contests().contest(entry.contest);
  battle_training_.stage =
      match->world->live().stages.at(entry.area)[contest && contest->night ? 1 : 0];
  auto tuning = battle_training_.battle.scenes;
  if (const auto stage = stages_.find(battle_training_.stage); stage != stages_.end()) {
    apply_stage(tuning, stage->second);
  }
  if (!match->entrances) {
    auto npc_tuning = tuning;
    npc_tuning.entrance = false;
    npc_tuning.exit = false;
    if (const auto file = roots_.find_asset(tuning.tables)) {
      match->entrances =
          std::make_unique<BattleScene>(parse_battle_scenes(json::read_file(*file)), npc_tuning);
    }
  }
  match->join(coop_slot_, battle_training_.robots.at("player"));
  // Distinct gates where available; the scene's own opening/closing timing is reused.
  battle_training_.battle.scenes.gate = (entry.gate + coop_slot_);
  return make_training(battle_training_, rumbling_battle_.lesson, match->battle, entry.area, true,
                       match);
}

} // namespace phantomcrash
