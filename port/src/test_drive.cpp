#include "phantomcrash/test_drive.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace phantomcrash {
namespace {

// A hangar slot, the battle's weapon slot it fires from and its place in
// the robot's parts. The first arm is the right-hand weapon.
struct SlotWeapon {
  PartSlot part;
  WeaponSlot weapon;
  const char *name;
};

constexpr std::array<SlotWeapon, weapon_slots> slot_weapons{{
    {PartSlot::right_arm, WeaponSlot::right_arm, "right_arm"},
    {PartSlot::left_arm, WeaponSlot::left_arm, "left_arm"},
    {PartSlot::right_shoulder, WeaponSlot::right_shoulder, "right_shoulder"},
    {PartSlot::left_shoulder, WeaponSlot::left_shoulder, "left_shoulder"},
}};

WeaponKind kind_named(const std::string &name) {
  static const std::map<std::string, WeaponKind> kinds{{"gun", WeaponKind::gun},
                                                       {"melee", WeaponKind::melee},
                                                       {"missile", WeaponKind::missile},
                                                       {"grenade", WeaponKind::grenade}};
  const auto found = kinds.find(name);
  if (found == kinds.end()) {
    throw std::runtime_error("no weapon kind is named " + name);
  }
  return found->second;
}

void fit_part(RobotParts &robot, const PartSlot slot, const int id) {
  switch (slot) {
  case PartSlot::right_arm:
    robot.arms[0] = id;
    break;
  case PartSlot::left_arm:
    robot.arms[1] = id;
    break;
  case PartSlot::right_shoulder:
    robot.shoulders[0] = id;
    break;
  case PartSlot::left_shoulder:
    robot.shoulders[1] = id;
    break;
  default:
    break;
  }
}

} // namespace

TestDriveScript parse_test_drive(const json::JsonValue &document) {
  try {
    TestDriveScript script;
    script.scene = static_cast<int>(document.number_or("scene", script.scene));
    script.contest = static_cast<int>(document.number_or("contest", script.contest));
    script.return_to = json::required(document, "return_to").string();
    const auto &lesson = json::required(document, "lesson");
    script.lesson.title = lesson.string_or("title", script.lesson.title);
    script.lesson.mode = static_cast<int>(lesson.number_or("mode", script.lesson.mode));
    for (const auto &[kind, name] : json::required(document, "weapon_templates").object()) {
      script.templates[kind_named(kind)] = name.string();
    }
    if (const auto *choices = document.find("stage_choices")) {
      for (const auto &choice : choices->array()) {
        script.stage_choices.emplace_back(json::required(choice, "stage").string(),
                                          json::required(choice, "label").string());
      }
    }
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("test_drive.json: ") + error.what());
  }
}

WeaponKind record_kind(const WeaponRecord &record) {
  if (record.flight == weapon_flight_missile) {
    return WeaponKind::missile;
  }
  if (record.flight == weapon_flight_lobbed) {
    return WeaponKind::grenade;
  }
  return record.kind == hud_kind::melee ? WeaponKind::melee : WeaponKind::gun;
}

std::optional<WeaponTuning> record_weapon(const TestDriveScript &drive,
                                          const std::map<std::string, WeaponTuning> &base,
                                          const PartTables &parts, const PartRef &ref,
                                          std::string name) {
  const WeaponRecord *record = parts.weapon(ref);
  if (record == nullptr) {
    return std::nullopt;
  }
  const WeaponKind kind = record_kind(*record);
  WeaponTuning tuning;
  if (const auto kind_name = drive.templates.find(kind); kind_name != drive.templates.end()) {
    if (const auto found = base.find(kind_name->second); found != base.end()) {
      tuning = found->second;
    }
  }
  tuning.name = std::move(name);
  tuning.label = record->name;
  tuning.kind = kind;
  tuning.part = ref;
  tuning.source_melee_kind = record->source_melee_kind;
  apply_weapon_stats(tuning, weapon_stats(*record, ref.level));
  return tuning;
}

TrainingScript test_drive_training(const TrainingScript &base, const TestDriveScript &drive,
                                   const Garage &garage, const int hangar,
                                   const PartTables &parts) {
  TrainingScript script = base;
  script.return_to = drive.return_to;
  const Hangar &bay =
      garage.hangars[static_cast<std::size_t>(std::clamp(hangar, 0, garage_hangar_count - 1))];
  const auto entry = [&garage, &bay](const PartSlot slot) -> const StockItem * {
    const auto found = garage.stock.find(bay.part(slot));
    return found != garage.stock.end() ? &found->second : nullptr;
  };
  RobotParts robot;
  robot.colour = bay.paint;
  robot.emblem = bay.emblem;
  TrainingRobot player = base.battle.player;
  player.weapons = {};
  if (const StockItem *body = entry(PartSlot::body)) {
    robot.body = body->id;
    player.body_level = body->level;
  }
  if (const StockItem *legs = entry(PartSlot::legs)) {
    robot.legs = legs->id;
    player.legs_level = legs->level;
  }
  if (const StockItem *chip = entry(PartSlot::chip)) {
    player.chip = chip->id;
    player.chip_level = chip->level;
    player.chip_name = chip->name;
  }
  player.modules.clear();
  for (const int stock : bay.modules) {
    if (const auto found = garage.stock.find(stock); found != garage.stock.end()) {
      player.modules.push_back(found->second.id);
    }
  }
  auto &weapons = script.battle.settings.weapons;
  for (const SlotWeapon &slot : slot_weapons) {
    const StockItem *item = entry(slot.part);
    if (item == nullptr) {
      continue;
    }
    const WeaponTable table =
        item->kind == PartKind::arm ? WeaponTable::arm : WeaponTable::shoulder;
    std::optional<WeaponTuning> tuning =
        record_weapon(drive, base.battle.settings.weapons, parts, {table, item->id, item->level},
                      std::string("test_drive_") + slot.name);
    if (!tuning) {
      continue;
    }
    fit_part(robot, slot.part, item->id);
    player.weapons[static_cast<std::size_t>(slot.weapon)] = tuning->name;
    weapons[tuning->name] = std::move(*tuning);
  }
  script.robots["player"] = robot;
  script.battle.player = player;
  return script;
}

} // namespace phantomcrash
