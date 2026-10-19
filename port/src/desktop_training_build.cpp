#include "desktop_training.hpp"
#include "desktop_coop_matches.hpp"

#include "desktop_draw_list.hpp"
#include "desktop_event_text.hpp"
#include "desktop_text.hpp"
#include "desktop_training_robots.hpp"
#include "desktop_stage_objects.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <iterator>
#include <optional>
#include <utility>

namespace phantomcrash {

void TrainingScreen::build_battle(const TrainingContext &context) {
  const TrainingBattle &setup = script_.battle;
  EnemyPath path;
  if (const auto file = context.roots.find_asset(setup.enemy_path)) {
    try {
      path = parse_enemy_path(json::read_file(*file));
    } catch (const std::exception &error) {
      SDL_Log("%s: %s", setup.enemy_path.c_str(), error.what());
    }
  } else {
    SDL_Log("Enemy path %s is missing; convert the robots again", setup.enemy_path.c_str());
  }
  const bool drones = !path.entries().empty();
  const std::optional<PartTables> parts = load_part_tables(context.roots);
  const bool modules = !setup.player.modules.empty() || !setup.drone.modules.empty() ||
                       std::ranges::any_of(setup.riders, [](const TrainingRider &rider) {
                         return !rider.robot.modules.empty();
                       });
  const std::optional<GarageTables> garage =
      modules ? load_garage_tables(context.roots) : std::nullopt;
  BattleSettings settings = setup.settings;
  if (parts) {
    for (const std::string &name : apply_weapon_records(settings.weapons, *parts)) {
      SDL_Log("Weapon %s: its part is not in %s", name.c_str(), part_tables_asset);
    }
  }
  const float player_hp = robot_hp(script_, parts, "player", setup.player, true);
  live_world_ = coop_match_           ? coop_match_->world.get()
                : context.live_battle ? context.live_world
                                      : nullptr;
  live_area_ = context.live_area;
  if (live_world_ != nullptr) {
    battle_ = context.live_battle;
    battle_->select_player(player_index_);
    if (!online()) {
      seat_player(settings, player_hp);
    }
  } else {
    battle_ = std::make_shared<Battle>(std::move(settings), std::move(path));
    if (context.stage != nullptr && !context.stage->collision.empty()) {
      load_stage_collision(context.roots, *context.stage, *battle_);
    }
    battle_->add_robot("player", 0, setup.player_start, setup.player_heading, setup.player.weapons,
                       player_hp);
    if (vs_) {
      seat_riders(parts, garage);
      if (context.stage && !context.stage->player_spawns.empty()) {
        const auto &spawns = context.stage->player_spawns;
        for (std::size_t i = 0; i < setup.riders.size(); ++i) {
          battle_->place(static_cast<int>(i), spawns[i % spawns.size()], setup.player_heading);
        }
      }
    }
  }
  if (!online()) {
    battle_->set_tag(player_index_,
                     coop_match_ ? context.player_name : pilot_tag(parts, setup.player.pilot));
    battle_->set_combat(player_index_,
                        robot_combat(script_, parts, garage, "player", setup.player));
  }
  const float drone_hp = robot_hp(script_, parts, "drone", setup.drone, false);
  const CombatStats drone_combat = robot_combat(script_, parts, garage, "drone", setup.drone);
  std::vector<std::pair<int, const TrainingRobot *>> chips{{player_index_, &setup.player}};
  for (int index = 0; drones && live_world_ == nullptr && index < setup.drones; ++index) {
    const int drone = battle_->add_drone("drone" + std::to_string(index), drone_hp);
    battle_->set_tag(drone, pilot_tag(parts, setup.drone.pilot));
    battle_->set_combat(drone, drone_combat);
    battle_->set_boss(drone, setup.drone.boss);
    chips.emplace_back(drone, &setup.drone);
  }
  hud_ = make_battle_hud(textures_, font_, context.roots, screens_, player_index_);
  // Every robot's chip: its lock time and the boxes it locks in without a
  // drawn reticle (0x04e6b8 builds every robot's sight).
  for (std::size_t rider = 1; vs_ && live_world_ == nullptr && rider < setup.riders.size();
       ++rider) {
    chips.emplace_back(static_cast<int>(rider), &setup.riders[rider].robot);
  }
  for (const auto &[index, robot] : chips) {
    if (online()) {
      continue;
    }
    if (const ChipRecord *chip = parts ? parts->chip(robot->chip) : nullptr) {
      const ChipStats stats = chip_stats(*chip, robot->chip_level);
      battle_->set_lock_seconds(index, stats.lock_frames / part_frames_per_second);
      battle_->set_sight(index, hud_->sight_boxes(stats));
    }
  }
  build_split(context.roots);
  chatter_ = load_chatter_window(textures_, font_, context.roots, talk_, tuning_, setup);
  if (const ChipRecord *chip = parts ? parts->chip(setup.player.chip) : nullptr) {
    hud_->set_fire_control(chip_stats(*chip, setup.player.chip_level));
    if (chatter_) {
      chatter_->set_speaker(player_index_,
                            chip_speaker(*chip, setup.player.chip_name, context.player_name));
    }
  }
  if (world_ == nullptr || !world_->available() || context.robots == nullptr ||
      !context.robots->available()) {
    return;
  }
  view_ = std::make_unique<BattleView>(*world_, *context.robots);
  view_->set_optical(setup.optical);
  view_->set_shield(setup.shield_look);
  view_->set_shadows(setup.shadows, !splits_.empty());
  try {
    const auto player = script_.robots.find("player");
    const auto drone = script_.robots.find("drone");
    if (coop_match_) {
      for (std::size_t index = 0; index < battle_->robots().size(); ++index) {
        coop_looks_.push_back(coop_match_->look(static_cast<int>(index)));
        view_->add(coop_looks_.back());
      }
    } else {
      view_->add(player == script_.robots.end() ? RobotParts{} : player->second);
      for (std::size_t rider = 1; vs_ && rider < setup.riders.size(); ++rider) {
        view_->add(script_.robots.at(setup.riders[rider].name));
      }
    }
    if (live_world_ != nullptr && !coop_match_) {
      add_live_views();
    }
    for (std::size_t index = view_->size(); index < battle_->robots().size(); ++index) {
      view_->add(drone == script_.robots.end() ? RobotParts{} : drone->second);
    }
  } catch (const std::exception &error) {
    SDL_Log("Training robots: %s", error.what());
    view_.reset();
    return;
  }
  // Apply stride speed and load; live opponents already have theirs.
  if (online()) {
    return;
  }
  for (std::size_t index = 0; index < view_->size(); ++index) {
    battle_->set_radius(static_cast<int>(index), view_->radius(index, setup.settings.robot_radius));
    if (index != static_cast<std::size_t>(player_index_) && live_world_ != nullptr) {
      continue;
    }
    MovementTuning movement = setup.stride_speeds ? view_->movement(index, setup.settings.movement)
                                                  : setup.settings.movement;
    if (parts) {
      const TrainingRider *rider =
          vs_ && index < setup.riders.size() ? &setup.riders[index] : nullptr;
      const RobotLoadout loadout = rider != nullptr
                                       ? training_loadout(script_, rider->name, rider->robot)
                                   : index == static_cast<std::size_t>(player_index_)
                                       ? training_loadout(script_, "player", setup.player)
                                       : training_loadout(script_, "drone", setup.drone);
      movement = loaded_movement(*parts, loadout, load_stats(*parts, loadout), movement);
    }
    battle_->set_movement(static_cast<int>(index), movement);
  }
}

} // namespace phantomcrash
