#include "phantomcrash/robot_totals.hpp"

#include "phantomcrash/robot_load.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>

namespace phantomcrash {
namespace {

// A module cuts no more than all of a laser's damage (0x0b9eef).
constexpr double full_reduction = 100.0;

const StockItem *fitted_entry(const Garage &owned, const int stock) {
  const auto entry = owned.stock.find(stock);
  return entry != owned.stock.end() ? &entry->second : nullptr;
}

const ModuleRecord *module_record(const GarageTables &garage, const StockItem *entry) {
  if (entry == nullptr || entry->id < 0 || entry->id >= static_cast<int>(garage.modules.size())) {
    return nullptr;
  }
  return &garage.modules[static_cast<std::size_t>(entry->id)];
}

void add_modules(const PartStatsTable &table, const GarageTables &garage, const Garage &owned,
                 const Hangar &fitted, RobotTotals &totals) {
  for (const int stock : fitted.modules) {
    const ModuleRecord *module = module_record(garage, fitted_entry(owned, stock));
    if (module == nullptr) {
      continue;
    }
    totals.weight += module->weight * table.hundredths;
    totals.armour += module->armour;
    totals.sockets_used += std::max(module->sockets, 1);
    totals.active_seconds += module->active_frames / table.display_rate;
    totals.charge_seconds += module->charge_frames / table.display_rate;
    totals.laser_reduction += module->laser_reduction;
  }
  totals.laser_reduction = std::clamp(totals.laser_reduction, 0.0, full_reduction);
}

} // namespace

RobotTotals robot_totals(const PartStatsTable &table, const PartTables &parts,
                         const GarageTables &garage, const Garage &owned, const Hangar &fitted) {
  RobotTotals totals;
  const RobotLoadout loadout = hangar_loadout(owned, fitted);
  const LoadStats load = load_stats(parts, loadout);
  totals.weight = load.weight * table.hundredths;
  totals.capacity = load.capacity * table.hundredths;
  totals.armour =
      robot_armour(parts, loadout.body, loadout.body_level, loadout.legs, loadout.legs_level, true);
  if (loadout.body >= 0 && loadout.body < static_cast<int>(parts.bodies.size())) {
    totals.sockets = static_cast<int>(std::trunc(
        parts.bodies[static_cast<std::size_t>(loadout.body)].sockets.value(loadout.body_level)));
  }
  add_modules(table, garage, owned, fitted, totals);
  if (const StockItem *chip = fitted_entry(owned, fitted.part(PartSlot::chip))) {
    totals.has_chip = true;
    totals.chip_level = chip->level;
    totals.chip_name = chip->name;
    if (totals.chip_name.empty()) {
      if (const ChipRecord *record = parts.chip(chip->id)) {
        totals.chip_name = record->name;
      }
    }
  }
  return totals;
}

double total_value(const RobotTotals &totals, const TotalRow row) {
  switch (row) {
  case TotalRow::weight:
    return totals.weight;
  case TotalRow::capacity:
    return totals.capacity;
  case TotalRow::armour:
    return totals.armour;
  case TotalRow::sockets:
    return totals.sockets;
  case TotalRow::overclock:
    return totals.charge_seconds;
  case TotalRow::laser_reduction:
    return totals.laser_reduction;
  case TotalRow::level:
    return totals.chip_level;
  case TotalRow::chip_name:
    break;
  }
  return 0.0;
}

int total_decimals(const PartStatsTable &table, const TotalRow row) {
  const auto decimals = [&table](const StatRow stat) {
    return table.decimals[static_cast<std::size_t>(stat)];
  };
  switch (row) {
  case TotalRow::weight:
    return decimals(StatRow::weight);
  case TotalRow::capacity:
    return decimals(StatRow::capacity);
  case TotalRow::armour:
    return decimals(StatRow::armour);
  case TotalRow::sockets:
    return decimals(StatRow::sockets);
  case TotalRow::overclock:
    return decimals(StatRow::charge_time);
  case TotalRow::laser_reduction:
    return decimals(StatRow::laser_reduction);
  case TotalRow::level:
    return decimals(StatRow::level);
  case TotalRow::chip_name:
    break;
  }
  return 0;
}

TotalRow total_row_named(const std::string &name) {
  static const std::map<std::string, TotalRow> rows{
      {"weight", TotalRow::weight},       {"capacity", TotalRow::capacity},
      {"armour", TotalRow::armour},       {"sockets", TotalRow::sockets},
      {"overclock", TotalRow::overclock}, {"laser_reduction", TotalRow::laser_reduction},
      {"level", TotalRow::level},         {"chip_name", TotalRow::chip_name},
  };
  const auto found = rows.find(name);
  if (found == rows.end()) {
    throw std::runtime_error("no total line " + name);
  }
  return found->second;
}

} // namespace phantomcrash
