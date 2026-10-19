#include "desktop_training_robots.hpp"

#include "phantomcrash/added_chips.hpp"
#include "phantomcrash/stage_fixes.hpp"

#include <SDL3/SDL.h>

namespace phantomcrash {

std::optional<PartTables> load_part_tables(const DataRoots &roots) {
  try {
    const auto table = read_converted_table(roots, part_tables_asset);
    if (!table) {
      SDL_Log("%s is missing; convert the robots again", part_tables_asset);
      return std::nullopt;
    }
    return parse_part_tables(*table);
  } catch (const std::exception &error) {
    SDL_Log("%s: %s", part_tables_asset, error.what());
  }
  return std::nullopt;
}

std::optional<GarageTables> load_garage_tables(const DataRoots &roots) {
  try {
    const auto table = read_converted_table(roots, "tables/garage.json");
    if (!table) {
      SDL_Log("tables/garage.json is missing; robots fit no modules");
      return std::nullopt;
    }
    return parse_garage_tables(*table);
  } catch (const std::exception &error) {
    SDL_Log("tables/garage.json: %s", error.what());
  }
  return std::nullopt;
}

float robot_hp(const TrainingScript &script, const std::optional<PartTables> &parts,
               const char *name, const TrainingRobot &robot, const bool player) {
  const auto found = script.robots.find(name);
  if (!parts || !script.battle.armour_from_parts || found == script.robots.end()) {
    return robot.hp;
  }
  const float armour = robot_armour(*parts, found->second.body, robot.body_level,
                                    found->second.legs, robot.legs_level, player);
  return armour > 0.0F ? armour : robot.hp;
}

CombatStats robot_combat(const TrainingScript &script, const std::optional<PartTables> &parts,
                         const std::optional<GarageTables> &garage, const char *name,
                         const TrainingRobot &robot) {
  const auto found = script.robots.find(name);
  if (!parts) {
    return {};
  }
  const int legs = found == script.robots.end() ? -1 : found->second.legs;
  CombatStats combat = combat_stats(*parts, robot.chip, robot.chip_level, legs, robot.legs_level);
  if (garage) {
    add_module_bonuses(combat, *garage, robot.modules);
  }
  return combat;
}

void load_stage_collision(const DataRoots &roots, const StageDefinition &stage, Battle &battle) {
  const std::string &asset = stage.collision;
  const auto file = roots.find_asset(asset);
  if (!file) {
    SDL_Log("Stage collision %s is missing; convert the robots again", asset.c_str());
    return;
  }
  try {
    battle.set_stage(fixed_stage_collision(roots, stage, json::read_file(*file)));
  } catch (const std::exception &error) {
    SDL_Log("%s: %s", asset.c_str(), error.what());
  }
}

std::string pilot_tag(const std::optional<PartTables> &parts, const int pilot) {
  const PilotRecord *record = parts ? parts->pilot(pilot) : nullptr;
  return record != nullptr ? record->tag : std::string{};
}

} // namespace phantomcrash
