#include "phantomcrash/garage_tables.hpp"

#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/robot_parts.hpp"

#include <algorithm>
#include <stdexcept>

namespace phantomcrash {
namespace {

using json::JsonValue;

int integer(const JsonValue &value, const char *key) {
  return static_cast<int>(json::required(value, key).number());
}

std::string name_of(const JsonValue &value) {
  const JsonValue *name = value.find("name");
  return name != nullptr && name->is_string() ? name->string() : std::string{};
}

RobotRecord robot_record(const JsonValue &value) {
  RobotRecord out;
  out.name = name_of(value);
  const JsonValue &parts = json::required(value, "parts");
  for (std::size_t slot = 0; slot < part_slot_names.size(); ++slot) {
    const JsonValue *part = find_part_slot(parts, slot);
    if (part != nullptr && !part->is_null()) {
      out.parts[slot] = RobotPart{integer(*part, "id"), integer(*part, "level")};
    }
  }
  for (const JsonValue &module : json::required(value, "modules").array()) {
    out.modules.push_back(static_cast<int>(module.number()));
  }
  out.paint = integer(value, "paint");
  out.emblem = integer(value, "emblem");
  return out;
}

std::array<LineupEntry, 3> lineup(const JsonValue &value) {
  std::array<LineupEntry, 3> out{};
  const auto &entries = value.array();
  if (entries.size() != out.size()) {
    throw std::runtime_error("a dealer lineup needs three SVs");
  }
  for (std::size_t k = 0; k < out.size(); ++k) {
    out[k] = {integer(entries[k], "robot"), integer(entries[k], "picture")};
  }
  return out;
}

template <typename Record, typename Parse>
std::vector<Record> records(const JsonValue &document, const char *key, Parse parse) {
  std::vector<Record> out;
  for (const JsonValue &entry : json::required(document, key).array()) {
    try {
      out.push_back(parse(entry));
    } catch (const std::exception &error) {
      throw std::runtime_error(std::string(key) + " " + std::to_string(out.size()) + ": " +
                               error.what());
    }
  }
  return out;
}

template <typename Record> const Record *at(const std::vector<Record> &list, const int index) {
  return index >= 0 && index < static_cast<int>(list.size())
             ? &list[static_cast<std::size_t>(index)]
             : nullptr;
}

const LevelCurve *price_curve(const PartTables &parts, const PartKind kind, const int id) {
  switch (kind) {
  case PartKind::body:
    return at(parts.bodies, id) != nullptr ? &at(parts.bodies, id)->price : nullptr;
  case PartKind::legs:
    return at(parts.legs, id) != nullptr ? &at(parts.legs, id)->price : nullptr;
  case PartKind::arm:
    return at(parts.arm_weapons, id) != nullptr ? &at(parts.arm_weapons, id)->price : nullptr;
  case PartKind::shoulder:
    return at(parts.shoulder_weapons, id) != nullptr ? &at(parts.shoulder_weapons, id)->price
                                                     : nullptr;
  case PartKind::chip:
    return at(parts.chips, id) != nullptr ? &at(parts.chips, id)->price : nullptr;
  case PartKind::module:
    return nullptr;
  }
  return nullptr;
}

} // namespace

int RobotRecord::stock_needed() const {
  int needed = static_cast<int>(modules.size());
  for (const auto &part : parts) {
    needed += part ? 1 : 0;
  }
  return needed;
}

bool RobotRecord::complete() const {
  return part(PartSlot::body) && part(PartSlot::legs) && part(PartSlot::chip);
}

const RobotRecord *GarageTables::robot(const int index) const {
  return at(robots, index);
}

GarageTables parse_garage_tables(const JsonValue &document) {
  GarageTables out;
  out.robots = records<RobotRecord>(document, "robots", robot_record);
  out.dealer_lineups = records<std::array<LineupEntry, 3>>(document, "dealer_lineups", lineup);
  out.modules = records<ModuleRecord>(document, "modules", [](const JsonValue &value) {
    const auto field = [&value](const char *key, const double fallback) {
      return static_cast<int>(value.number_or(key, fallback));
    };
    ModuleRecord module{name_of(value), static_cast<float>(value.number_or("weight", 0.0)),
                        integer(value, "price")};
    module.armour = field("armour", 0.0);
    // 0x0bb5d3: a module whose byte is 0 still takes a hole.
    module.sockets = std::max(field("sockets", 1.0), 1);
    // The extractor names these after the robot fields they add to.
    module.critical_percent = static_cast<float>(value.number_or("robot_4538", 0.0));
    module.knockdown_resistance = static_cast<float>(value.number_or("robot_4540", 0.0));
    module.laser_reduction = std::clamp(field("laser_reduction", 0.0), 0, 100);
    module.active_frames = field("active_frames", 0.0);
    module.charge_frames = field("charge_frames", 0.0);
    return module;
  });
  const auto entry = [](const JsonValue &value) {
    return PriceEntry{name_of(value), integer(value, "price"),
                      static_cast<int>(value.number_or("index", 0.0))};
  };
  out.paints = records<PriceEntry>(document, "paints", entry);
  out.emblems = records<PriceEntry>(document, "emblems", entry);
  // Wild Machines' stock; conversions before it lack these.
  const auto stock_lineup = [](const JsonValue &value) {
    std::vector<RobotPart> items;
    for (const JsonValue &item : value.array()) {
      items.push_back({integer(item, "id"), integer(item, "level")});
    }
    return items;
  };
  if (document.find("chip_lineups") != nullptr) {
    out.chip_lineups = records<std::vector<RobotPart>>(document, "chip_lineups", stock_lineup);
    out.module_lineups = records<std::vector<RobotPart>>(document, "module_lineups", stock_lineup);
    out.used_sv_lineups =
        records<std::vector<int>>(document, "used_sv_lineups", [](const JsonValue &value) {
          std::vector<int> robots;
          for (const JsonValue &robot : value.array()) {
            robots.push_back(static_cast<int>(robot.number()));
          }
          return robots;
        });
  }
  if (document.find("boss_chips") != nullptr) {
    out.boss_chips = records<BossChip>(document, "boss_chips", [](const JsonValue &value) {
      BossChip chip{integer(value, "id"), integer(value, "level"), {}};
      for (const JsonValue &contest : json::required(value, "contests").array()) {
        chip.contests.push_back(static_cast<int>(contest.number()));
      }
      return chip;
    });
  }
  return out;
}

std::int64_t part_price(const PartTables &parts, const GarageTables &garage, const PartKind kind,
                        const int id, const int level) {
  if (kind == PartKind::module) {
    const ModuleRecord *module = at(garage.modules, id);
    return module != nullptr ? module->price : 0;
  }
  const LevelCurve *curve = price_curve(parts, kind, id);
  return curve != nullptr ? static_cast<std::int64_t>(curve->value(level)) : 0;
}

std::int64_t robot_price(const PartTables &parts, const GarageTables &garage,
                         const RobotRecord &robot) {
  std::int64_t total = 0;
  for (std::size_t slot = 0; slot < robot.parts.size(); ++slot) {
    if (const auto &part = robot.parts[slot]) {
      total +=
          part_price(parts, garage, slot_kind(static_cast<PartSlot>(slot)), part->id, part->level);
    }
  }
  for (const int module : robot.modules) {
    total += part_price(parts, garage, PartKind::module, module, 0);
  }
  if (const PriceEntry *paint = at(garage.paints, robot.paint)) {
    total += paint->price;
  }
  if (const PriceEntry *emblem = at(garage.emblems, robot.emblem)) {
    total += emblem->price;
  }
  return total;
}

std::string part_name(const PartTables &parts, const GarageTables &garage, const PartKind kind,
                      const int id) {
  const auto named = [](const auto *record) { return record != nullptr ? record->name : ""; };
  switch (kind) {
  case PartKind::body:
    return named(at(parts.bodies, id));
  case PartKind::legs:
    return named(at(parts.legs, id));
  case PartKind::arm:
    return named(at(parts.arm_weapons, id));
  case PartKind::shoulder:
    return named(at(parts.shoulder_weapons, id));
  case PartKind::chip:
    return named(at(parts.chips, id));
  case PartKind::module:
    return named(at(garage.modules, id));
  }
  return {};
}

void add_module_bonuses(CombatStats &combat, const GarageTables &garage,
                        const std::vector<int> &modules) {
  float active_frames = 0, charge_frames = 0, laser_reduction = 0;
  for (const int id : modules) {
    if (const ModuleRecord *module = at(garage.modules, id)) {
      active_frames += module->active_frames;
      charge_frames += module->charge_frames;
      combat.critical_percent += module->critical_percent;
      combat.knockdown_resistance += module->knockdown_resistance;
      laser_reduction += static_cast<float>(module->laser_reduction);
    }
  }
  // 0x075d4a: robot +0x4544, the modules' +0x20 added, kept to 0..100 and
  // made a share of a beam's damage.
  constexpr float full_reduction = 100.0F;
  combat.beam_resistance = std::clamp(laser_reduction, 0.0F, full_reduction) / full_reduction;
  // 0x814d7 sums module +0x22/+0x24 before 0x74db0 initializes O.C timers.
  constexpr float display_frames_per_second = 60.0F;
  combat.camouflage_seconds = std::max(active_frames, 0.0F) / display_frames_per_second;
  combat.camouflage_recharge_seconds = std::max(charge_frames, 0.0F) / display_frames_per_second;
}

int install_robot(Garage &garage, const int hangar, const RobotRecord &robot,
                  const PartTables &parts, const GarageTables &tables) {
  if (hangar < 0 || hangar >= garage_hangar_count || robot.stock_needed() > garage.free_stock()) {
    return -1;
  }
  Hangar &bay = garage.hangars[static_cast<std::size_t>(hangar)];
  bay.parts.fill(-1);
  bay.modules.clear();
  for (std::size_t slot = 0; slot < robot.parts.size(); ++slot) {
    if (const auto &part = robot.parts[slot]) {
      const PartKind kind = slot_kind(static_cast<PartSlot>(slot));
      const std::string name =
          kind == PartKind::chip ? part_name(parts, tables, kind, part->id) : std::string{};
      bay.parts[slot] = garage.add_stock({kind, part->id, part->level, name, hangar});
    }
  }
  for (const int module : robot.modules) {
    bay.modules.push_back(garage.add_stock({PartKind::module, module, 0, {}, hangar}));
  }
  bay.name = robot.name;
  bay.paint = robot.paint;
  bay.emblem = robot.emblem;
  return bay.part(PartSlot::chip);
}

} // namespace phantomcrash
