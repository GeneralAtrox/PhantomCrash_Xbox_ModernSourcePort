#include "ai_sim_world.hpp"

#include "phantomcrash/added_chips.hpp"
#include "phantomcrash/enemy_path.hpp"
#include "phantomcrash/garage.hpp"
#include "phantomcrash/robot.hpp"
#include "phantomcrash/robot_combat.hpp"
#include "phantomcrash/stage_collision.hpp"
#include "phantomcrash/stage_fixes.hpp"
#include "phantomcrash/stage_doors.hpp"
#include "phantomcrash/stage_objects.hpp"
#include "phantomcrash/supply_drop.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace phantomcrash::ai_sim {
namespace {

// The enemy path's entry segments, at the arena's edge (rumbling_world.cpp).
constexpr int entry_kind = 0;
// A robot whose record gives it no armour (rumbling_world.cpp).
constexpr float missing_armour = 1000.0F;
// The stand-in player's steering, as the legacy drones steer (battle.cpp).
constexpr float steer_gain = 2.0F;
constexpr float walk_cone = 0.7F;

json::JsonValue converted(const DataRoots &roots, const char *relative) {
  auto table = read_converted_table(roots, relative);
  if (!table) {
    throw std::runtime_error(std::string(relative) + " is missing; convert the game files");
  }
  return std::move(*table);
}

template <typename Parse>
auto stage_file(const DataRoots &roots, const std::string &asset, Parse parse)
    -> std::optional<decltype(parse(json::JsonValue{}))> {
  const auto file = asset.empty() ? std::nullopt : roots.find_asset(asset);
  if (!file) {
    std::cerr << "ai_sim: stage file " << asset << " is missing\n";
    return std::nullopt;
  }
  return parse(json::read_file(*file));
}

RumblingTables sim_tables(const SimData &data) {
  return {data.parts, data.garage,     data.contests,
          data.rules, data.test_drive, data.training.battle.settings.weapons,
          data.hud};
}

} // namespace

SimData::SimData(const std::filesystem::path &root) : roots(DataRoots::standard(root)) {
  contests = parse_contest_tables(converted(roots, "tables/town.json"));
  parts = parse_part_tables(converted(roots, "tables/parts.json"));
  garage = parse_garage_tables(converted(roots, "tables/garage.json"));
  rules = parse_rumbling_rules(roots.document("world/rumbling.json"));
  part_stats = load_part_stats(roots);
  training = parse_training(roots.document("world/training.json"));
  test_drive = parse_test_drive(roots.document("world/test_drive.json"));
  rumbling_battle = parse_rumbling_battle(roots.document("world/rumbling.json"));
  stages = parse_stages(roots.document("world/stages.json"));
  live = parse_rumbling_live(roots.document("world/rumbling.json"));
  live.ai.original = parse_original_ai(roots.document("world/robot_ai.json"));
  // The game builds the navigation grid on a worker thread and has it within
  // a second or so of play; at the sim's speed a battle could end first. Build
  // it before the battle starts instead (once per stage, then shared).
  live.ai.navigation.background = false;
  hud = parse_battle_hud_style(roots.document("ui/battle_hud.json"));

  const auto new_game = roots.document("story/new_game.json");
  if (const auto *start = new_game.find("quest")) {
    new_quest.day = static_cast<int>(start->number_or("day", 0));
    new_quest.money = static_cast<std::int64_t>(start->number_or("money", 0));
    if (const auto *stock = start->find("garage")) {
      new_quest.garage = garage_from_json(*stock);
    }
  }

  robots = std::make_unique<RobotLibrary>(roots);
  if (!robots->available()) {
    std::cerr << "ai_sim: robots are not converted; running without poses\n";
    robots.reset();
  }

  // As ScreenRunner::rumbling_world: the day's and the night's stage share
  // their ground and path.
  for (std::size_t area = 0; area < roster_areas; ++area) {
    const auto stage = stages.find(live.stages[area][0]);
    if (stage == stages.end()) {
      throw std::runtime_error("no stage " + live.stages[area][0] + " in stages.json");
    }
    area_stage_names[area] = stage->first;
    RumblingStage &target = area_stages[area];
    if (auto path = stage_file(roots, stage->second.enemy_path, parse_enemy_path)) {
      target.path = std::move(*path);
    }
    const auto parse_ground = [&](const json::JsonValue &value) {
      return fixed_stage_collision(roots, stage->second, value);
    };
    if (auto ground = stage_file(roots, stage->second.collision, parse_ground)) {
      target.collision = std::move(*ground);
    }
    target.objects = stage_file(roots, "tables/stage_objects.json", [&](const auto &document) {
      return parse_stage_objects(document, stage->second.scene_stage);
    });
  }
}

SimArena::SimArena(SimData &data, const ArenaOptions &options, const int area,
                   const std::uint32_t seed)
    : data_(data), options_(options), area_(area), quest_(data.new_quest),
      check_{data.rules, data.parts, data.garage, data.part_stats, false}, random_(seed) {
  data_.rules.roster.live_size = options.robots;
  quest_.day = options.day;
  BattleSettings settings = data.training.battle.settings;
  static_cast<void>(apply_weapon_records(settings.weapons, data.parts));
  world_ = std::make_unique<RumblingWorld>(check_, sim_tables(data), std::move(settings),
                                           data.area_stages, data.live, seed);
  world_->set_background(options.background);
  world_->update(quest_, 0.0);
  battle_ = world_->lend(area, quest_);
  if (!battle_) {
    return;
  }
  add_doors_and_supply();
  if (options_.player != PlayerMode::none) {
    seat_player();
  }
  bound_entrants_.assign(battle_->robots().size(), -1);
  follow_world();
}

const std::string &SimArena::stage_name() const {
  return data_.area_stage_names[static_cast<std::size_t>(area_)];
}

int SimArena::entrant(const int index) const {
  const RumblingRobot *robot = world_->robot(area_, index);
  return robot != nullptr ? robot->entrant : -1;
}

void SimArena::add_doors_and_supply() {
  SceneTuning scenes = data_.training.battle.scenes;
  if (const auto stage = data_.stages.find(stage_name()); stage != data_.stages.end()) {
    apply_stage(scenes, stage->second);
  }
  const auto file = data_.roots.find_asset(scenes.tables);
  if (!file) {
    return;
  }
  const json::JsonValue tables = json::read_file(*file);
  if (options_.doors && scenes.doors.enabled) {
    std::vector<DoorFrame> doors = parse_stage_doors(tables, scenes.stage);
    if (!doors.empty()) {
      battle_->set_doors(StageDoors(std::move(doors), scenes.doors));
    }
  }
  if (options_.supply) {
    SupplySettings supply = data_.training.battle.supply;
    supply.routes = parse_supply_routes(tables, scenes.stage);
    if (!supply.routes.empty()) {
      battle_->set_supply(std::move(supply));
    }
  }
}

void SimArena::seat_player() {
  int entrant = options_.player_entrant;
  if (entrant < 0) {
    // A typical opponent of the area: the roster's median level.
    std::vector<std::pair<int, int>> levels;
    for (const RosterMember &member : quest_.rosters[static_cast<std::size_t>(area_)].members) {
      const auto *record = data_.contests.entrant(member.entrant);
      levels.emplace_back(record != nullptr ? record->level : 0, member.entrant);
    }
    if (levels.empty()) {
      return;
    }
    std::ranges::sort(levels);
    entrant = levels[levels.size() / 2].second;
  }
  const RumblingRobot robot = rumbling_robot(sim_tables(data_), quest_, entrant, "player",
                                             data_.training.battle.settings.movement);
  std::array<std::string, weapon_slots> weapons{};
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    if (robot.weapons[slot]) {
      weapons[slot] = battle_->add_weapon(*robot.weapons[slot]).name;
    }
  }
  battle_->set_identity(0, "player", 0, robot.armour > 0.0F ? robot.armour : missing_armour);
  battle_->arm(0, weapons);
  battle_->set_combat(0, robot.combat);
  battle_->set_lock_seconds(0, robot.lock_seconds);
  battle_->set_sight(0, robot.sight);
  battle_->set_movement(0, robot.movement);
  battle_->bring_in(0);
  battle_->set_cheats({true, true}); // the stand-in never dies

  const EnemyPath &path = data_.area_stages[static_cast<std::size_t>(area_)].path;
  std::vector<int> starts;
  for (std::size_t segment = 0; segment < path.segments.size(); ++segment) {
    if (path.segments[segment].kind != entry_kind && !path.segments[segment].points.empty()) {
      starts.push_back(static_cast<int>(segment));
    }
  }
  if (!starts.empty()) {
    const int segment = starts[random_() % starts.size()];
    const auto &points = path.segments[static_cast<std::size_t>(segment)].points;
    battle_->place(0, points[random_() % points.size()], 0.0F);
    player_path_.start(path, segment);
  }
  bind_pose(0, robot.look);
}

void SimArena::bind_pose(const int index, const RobotParts &look) {
  Combatant &robot = battle_->robot(index);
  const float fallback = data_.training.battle.settings.robot_radius;
  if (!options_.poses || !data_.robots) {
    robot.pose.reset();
    battle_->set_radius(index, fallback);
    return;
  }
  try {
    RobotAssembly assembly =
        assemble_robot(data_.robots->catalogue(), data_.robots->skeletons(), look);
    battle_->set_radius(index, assembly.radius > 0.0F ? assembly.radius : fallback);
    robot.pose = std::make_shared<RobotCombat>(*data_.robots, std::move(assembly));
  } catch (const std::exception &error) {
    std::cerr << "ai_sim: robot " << index << " look: " << error.what() << '\n';
    robot.pose.reset();
    battle_->set_radius(index, fallback);
    return;
  }
  // As BattleView::bind: the new pose starts aimed straight ahead.
  std::array<Vec3, 4> targets;
  targets.fill(robot.aim_origin() +
               heading_forward(robot.mover.aim_heading()) * robot.pose->aim_distance());
  robot.pose->tick(robot.mover, {}, robot.lost, targets, 0);
}

void SimArena::follow_world() {
  if (world_->changes(area_) == changes_) {
    return;
  }
  changes_ = world_->changes(area_);
  for (std::size_t index = 1; index < bound_entrants_.size(); ++index) {
    const RumblingRobot *robot = world_->robot(area_, static_cast<int>(index));
    const int entrant = robot != nullptr ? robot->entrant : -1;
    if (entrant == bound_entrants_[index]) {
      continue;
    }
    bound_entrants_[index] = entrant;
    bind_pose(static_cast<int>(index), robot != nullptr ? robot->look : RobotParts{});
  }
}

RobotControl SimArena::player_control() {
  RobotControl control;
  const Combatant &robot = battle_->robots().front();
  if (options_.player != PlayerMode::patrol || !robot.alive || !player_path_.active()) {
    return control;
  }
  const BattleSettings &settings = battle_->settings();
  const EnemyPath &path = data_.area_stages[static_cast<std::size_t>(area_)].path;
  const Vec3 at = robot.mover.position();
  Vec3 offset = player_path_.update(path, at, settings.drone_reach, random_) - at;
  offset.y = 0.0F;
  if (length(offset) < 1e-3F) {
    return control;
  }
  const float error = wrap_angle(heading_toward(offset) - robot.mover.heading());
  control.move[0] = std::clamp(error * steer_gain, -1.0F, 1.0F);
  control.move[1] = std::abs(error) < walk_cone ? settings.drone_walk : 0.0F;
  return control;
}

void SimArena::tick(const float seconds) {
  world_->update(quest_, seconds);
  follow_world();
  battle_->tick(player_control(), {}, seconds);
  static_cast<void>(battle_->take_arrivals());
}

} // namespace phantomcrash::ai_sim
