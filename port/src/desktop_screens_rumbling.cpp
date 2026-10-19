// The runner's live RUMBLING (rumbling_world.hpp): the areas' battles run
// between screens from the moment a game is under way, and a RUMBLING
// entry's battle joins its area's.

#include "desktop_game_screens.hpp"
#include "desktop_screens.hpp"
#include "desktop_coop_matches.hpp"
#include "phantomcrash/stage_fixes.hpp"

#include <algorithm>
#include <random>

namespace phantomcrash {
namespace {

// A stage file the conversion writes, parsed; nothing (logged) without it.
template <typename Parse>
auto stage_file(const DataRoots &roots, const std::string &asset, Parse parse)
    -> std::optional<decltype(parse(json::JsonValue{}))> {
  const auto file = asset.empty() ? std::nullopt : roots.find_asset(asset);
  if (!file) {
    SDL_Log("Live RUMBLING: %s is missing; convert the game files", asset.c_str());
    return std::nullopt;
  }
  try {
    return parse(json::read_file(*file));
  } catch (const std::exception &error) {
    SDL_Log("%s: %s", asset.c_str(), error.what());
  }
  return std::nullopt;
}

} // namespace

void ScreenRunner::set_rumble_robots(const int robots) {
  rumble_robots_ = robots;
  if (coop_ && !cooperative()) {
    coop_->robots = robots;
  }
  game_->set_rumble_robots(robots);
}

RumblingWorld *ScreenRunner::rumbling_world() {
  if (rumbling_world_ || rumbling_failed_) {
    return rumbling_world_.get();
  }
  rumbling_failed_ = true; // until it has loaded
  RumblingLive live;
  try {
    live = parse_rumbling_live(roots_.document("world/rumbling.json"));
    live.ai.original = parse_original_ai(roots_.document("world/robot_ai.json"));
  } catch (const std::exception &error) {
    SDL_Log("%s", error.what());
    return nullptr;
  }
  std::array<RumblingStage, roster_areas> stages{};
  for (std::size_t area = 0; area < roster_areas; ++area) {
    // The day's and the night's stage share their ground and path.
    const auto stage = stages_.find(live.stages[area][0]);
    if (stage == stages_.end()) {
      SDL_Log("Live RUMBLING: no stage %s in stages.json", live.stages[area][0].c_str());
      continue;
    }
    if (auto path = stage_file(roots_, stage->second.enemy_path, parse_enemy_path)) {
      stages[area].path = std::move(*path);
    }
    const auto parse_ground = [&](const json::JsonValue &value) {
      return fixed_stage_collision(roots_, stage->second, value);
    };
    if (auto ground = stage_file(roots_, stage->second.collision, parse_ground)) {
      stages[area].collision = std::move(*ground);
    }
    stages[area].objects = stage_file(roots_, "tables/stage_objects.json", [&](const auto &data) {
      return parse_stage_objects(data, stage->second.scene_stage);
    });
  }
  BattleSettings settings = training_.battle.settings;
  // A contest is battle mode 0: its hits pay the rumbling.json hit_reward_ny.
  settings.hit_reward_ny = rumbling_battle_.hit_reward_ny;
  static_cast<void>(apply_weapon_records(settings.weapons, game_->part_tables()));
  const RegulationCheck check = game_->regulation_check();
  const RumblingTables tables{game_->part_tables(),
                              game_->garage_tables(),
                              game_->contests(),
                              check.rules,
                              test_drive_,
                              training_.battle.settings.weapons,
                              parse_battle_hud_style(roots_.document("ui/battle_hud.json"))};
  rumbling_world_ =
      std::make_shared<RumblingWorld>(check, tables, std::move(settings), std::move(stages),
                                      std::move(live), std::random_device{}());
  rumbling_failed_ = false;
  SDL_Log("Live RUMBLING: %d robots an area", rumble_robots_);
  return rumbling_world_.get();
}

void ScreenRunner::run_rumbling(const double seconds) {
  if (name_ == "coop_name") {
    return;
  }
  if (cooperative() && rumbling_world_ && rumbling_world_->lent() >= 0 && coop_->matches) {
    for (const auto &[key, match] : coop_->matches->matches) {
      if (match->world == rumbling_world_ && match->day != quest_.day) {
        rumbling_world_.reset();
        rumbling_failed_ = false;
        quest_.live_epoch = 0;
        break;
      }
    }
  }
  if (!rumbling_paused_) {
    rumbling_paused_.emplace();
    try {
      rumbling_paused_ = parse_rumbling_live(roots_.document("world/rumbling.json")).paused_screens;
    } catch (const std::exception &error) {
      SDL_Log("%s", error.what());
    }
  }
  // Nothing loads or runs until a game is under way.
  if (std::ranges::find(*rumbling_paused_, name_) != rumbling_paused_->end()) {
    return;
  }
  if (RumblingWorld *world = rumbling_world()) {
    if (cooperative() && world->lent() >= 0) {
      return; // Co-op's match owner runs it once.
    }
    const auto &fighting = world->live().fighting_screens;
    world->set_fighting(!fighting || std::ranges::find(*fighting, name_) != fighting->end());
    world->update(quest_, seconds);
  }
}

void ScreenRunner::join_rumbling(const RumblingEntry &entry) {
  if (entry.area < 0 || entry.area >= roster_areas) {
    return;
  }
  const AreaRoster &roster = quest_.rosters[static_cast<std::size_t>(entry.area)];
  const Contest *contest = game_->contests().contest(roster.contest);
  const bool night = contest != nullptr && contest->night;
  battle_training_.stage =
      rumbling_world_->live().stages[static_cast<std::size_t>(entry.area)][night ? 1 : 0];
}

} // namespace phantomcrash
