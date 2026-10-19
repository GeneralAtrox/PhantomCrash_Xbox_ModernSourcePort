#include "phantomcrash/rumbling_robot.hpp"

#include <algorithm>

namespace phantomcrash {
namespace {

// A record's weapon mount, the battle's slot it fires from and its table.
// The first arm is the right-hand weapon, as TEST DRIVE fits it.
struct Mount {
  PartSlot part;
  WeaponSlot weapon;
  WeaponTable table;
  const char *name;
};

constexpr std::array<Mount, weapon_slots> mounts{{
    {PartSlot::right_arm, WeaponSlot::right_arm, WeaponTable::arm, "right_arm"},
    {PartSlot::left_arm, WeaponSlot::left_arm, WeaponTable::arm, "left_arm"},
    {PartSlot::right_shoulder, WeaponSlot::right_shoulder, WeaponTable::shoulder, "right_shoulder"},
    {PartSlot::left_shoulder, WeaponSlot::left_shoulder, WeaponTable::shoulder, "left_shoulder"},
}};

void fit(RobotParts &look, const WeaponSlot slot, const int id) {
  switch (slot) {
  case WeaponSlot::right_arm:
    look.arms[0] = id;
    break;
  case WeaponSlot::left_arm:
    look.arms[1] = id;
    break;
  case WeaponSlot::right_shoulder:
    look.shoulders[0] = id;
    break;
  case WeaponSlot::left_shoulder:
    look.shoulders[1] = id;
    break;
  }
}

template <typename T> const T *at(const std::vector<T> &items, const int index) {
  return index >= 0 && index < static_cast<int>(items.size())
             ? &items[static_cast<std::size_t>(index)]
             : nullptr;
}

} // namespace

int raised_level(const int level, const QuestSave &quest) {
  return quest.cleared ? std::min(level + quest.laps, part_level_max) : level;
}

RumblingRobot rumbling_robot(const RumblingTables &tables, const QuestSave &quest,
                             const int entrant, const std::string &prefix,
                             const MovementTuning &movement, const int level_offset) {
  const auto level = [&](int base) {
    return std::clamp(raised_level(base, quest) + std::clamp(level_offset, -99, 99),
                      -part_level_max, part_level_max);
  };
  RumblingRobot robot;
  robot.entrant = entrant;
  robot.movement = movement;
  const Entrant *who = tables.contests.entrant(entrant);
  if (who == nullptr) {
    return robot;
  }
  if (const PilotRecord *pilot = tables.parts.pilot(who->pilot)) {
    robot.name = pilot->robot_name;
    robot.tag = pilot->tag;
  }
  const RobotRecord *record = tables.garage.robot(who->robot);
  if (record == nullptr) {
    return robot;
  }
  if (robot.name.empty()) {
    robot.name = record->name;
  }
  RobotLoadout &loadout = robot.loadout;
  if (const auto &chip = record->part(PartSlot::chip)) {
    loadout.chip = chip->id;
    loadout.chip_level = level(chip->level);
  }
  if (const auto &body = record->part(PartSlot::body)) {
    loadout.body = robot.look.body = body->id;
    loadout.body_level = level(body->level);
  }
  if (const auto &legs = record->part(PartSlot::legs)) {
    loadout.legs = robot.look.legs = legs->id;
    loadout.legs_level = level(legs->level);
  }
  robot.look.colour = record->paint;
  robot.look.emblem = record->emblem;
  for (const Mount &mount : mounts) {
    const auto &part = record->part(mount.part);
    if (!part) {
      continue;
    }
    const PartRef ref{mount.table, part->id, level(part->level)};
    auto tuning =
        record_weapon(tables.drive, tables.templates, tables.parts, ref, prefix + "_" + mount.name);
    if (!tuning) {
      continue;
    }
    loadout.weapons.push_back(ref);
    fit(robot.look, mount.weapon, part->id);
    robot.weapons[static_cast<std::size_t>(mount.weapon)] = std::move(tuning);
  }
  robot.armour = robot_armour(tables.parts, loadout.body, loadout.body_level, loadout.legs,
                              loadout.legs_level, false);
  robot.combat = combat_stats(tables.parts, loadout.chip, loadout.chip_level, loadout.legs,
                              loadout.legs_level);
  add_module_bonuses(robot.combat, tables.garage, record->modules);
  if (const ChipRecord *chip = tables.parts.chip(loadout.chip)) {
    const ChipStats stats = chip_stats(*chip, loadout.chip_level);
    robot.lock_seconds = stats.lock_frames / part_frames_per_second;
    robot.sight = sight_boxes(tables.hud, stats);
  }
  robot.movement =
      loaded_movement(tables.parts, loadout, load_stats(tables.parts, loadout), movement);
  const RobotKind kind = robot_kind(tables.parts, tables.rules, *record);
  robot.maker = kind.maker;
  robot.legs_type = kind.legs_type;
  return robot;
}

RobotKind robot_kind(const PartTables &parts, const RumblingRules &rules,
                     const RobotRecord &record) {
  RobotKind kind;
  const auto &legs_part = record.part(PartSlot::legs);
  const ArmourRecord *legs = legs_part ? at(parts.legs, legs_part->id) : nullptr;
  if (legs != nullptr) {
    kind.maker = legs->maker;
    if (const int *type = at(rules.legs_types, legs->model)) {
      kind.legs_type = *type;
    }
  }
  return kind;
}

} // namespace phantomcrash
