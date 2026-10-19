#pragma once

#include "phantomcrash/battle.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/training_ground.hpp"

#include <optional>
#include <string>

namespace phantomcrash {

// The part and pilot records the robot converter extracts.
inline constexpr const char *part_tables_asset = "tables/parts.json";

[[nodiscard]] std::optional<PartTables> load_part_tables(const DataRoots &roots);
// The module records (tables/garage.json), when any robot fits a module.
[[nodiscard]] std::optional<GarageTables> load_garage_tables(const DataRoots &roots);

// A robot's armour: its body and legs records' at their levels (0x076114; the
// player's robot gets the player floors), or the script's hp without them.
[[nodiscard]] float robot_hp(const TrainingScript &script, const std::optional<PartTables> &parts,
                             const char *name, const TrainingRobot &robot, bool player);

// What a robot's chip and legs (at their levels) give it in a fight, with
// its modules' bonuses when `garage` has their records.
[[nodiscard]] CombatStats robot_combat(const TrainingScript &script,
                                       const std::optional<PartTables> &parts,
                                       const std::optional<GarageTables> &garage, const char *name,
                                       const TrainingRobot &robot);

// The name tag of a pilot record, empty for none.
[[nodiscard]] std::string pilot_tag(const std::optional<PartTables> &parts, int pilot);

// Gives the battle the stage's collision (converted), logging a problem.
void load_stage_collision(const DataRoots &roots, const StageDefinition &stage, Battle &battle);

} // namespace phantomcrash
