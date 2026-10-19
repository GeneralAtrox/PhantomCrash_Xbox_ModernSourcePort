#pragma once

#include "phantomcrash/garage.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/part_stats.hpp"
#include "phantomcrash/robot_parts.hpp"

#include <string>

namespace phantomcrash {

// A line of a robot's WHOLE PARAMETER (0x019489, by the bits of 0x1daf68).
enum class TotalRow {
  weight,          // TOTAL WEIGHT, tonnes (0x0b982f)
  capacity,        // WEIGHT CAPACITY: the legs', tonnes (0x0b9a80)
  armour,          // TOTAL DURABILITY (0x0b9af5)
  sockets,         // SOCKET HOLE: the body's (0x0b9c31)
  overclock,       // the modules' O.C ACT/CHARGE: the charge, the active time beside it
  laser_reduction, // TOTAL LASER REDUCTION, percent (0x0b9eef)
  level,           // the CHIP's LEVEL
  chip_name,       // its CHIP-NAME
};

// The whole robot a hangar's fitting makes.
struct RobotTotals {
  double weight{};          // every part and module (0x075f93)
  double capacity{};        // the legs' at their level
  double armour{};          // the body's and legs', raised to their floors, and the modules'
  int sockets{};            // the body's socket holes
  int sockets_used{};       // those its modules take (0x0ba689)
  double active_seconds{};  // the modules' O.C active times, added (0x0b9de3)
  double charge_seconds{};  // and their charge times (0x0b9e69)
  double laser_reduction{}; // percent, 0..100 (0x0b9eef)
  bool has_chip{};
  int chip_level{};
  std::string chip_name; // the CHIP's own name, or its record's
};

[[nodiscard]] RobotTotals robot_totals(const PartStatsTable &table, const PartTables &parts,
                                       const GarageTables &garage, const Garage &owned,
                                       const Hangar &fitted);

// The value a line shows (the CHIP's name has none).
[[nodiscard]] double total_value(const RobotTotals &totals, TotalRow row);

// The digits after the point a line shows, as the part stats' table has them.
[[nodiscard]] int total_decimals(const PartStatsTable &table, TotalRow row);

// Throws std::runtime_error for a name that is no line.
[[nodiscard]] TotalRow total_row_named(const std::string &name);

} // namespace phantomcrash
