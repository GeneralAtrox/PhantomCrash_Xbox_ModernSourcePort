#pragma once

#include "phantomcrash/garage.hpp"
#include "phantomcrash/json.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

struct CombatStats;
struct PartTables;

// A part in a robot record: its record and level.
struct RobotPart {
  int id{};
  int level{};

  bool operator==(const RobotPart &) const = default;
};

// A robot record (0x19f920, 0x3c bytes, 0x05fa83): a whole Scoobee as a
// dealer sells it or a team fields it.
struct RobotRecord {
  std::string name;
  std::array<std::optional<RobotPart>, robot_part_slots> parts;
  std::vector<int> modules;
  int paint{};
  int emblem{emblem_none};

  [[nodiscard]] const std::optional<RobotPart> &part(PartSlot slot) const {
    return parts[static_cast<std::size_t>(slot)];
  }
  // The stock entries it takes: its parts and modules (0x039501).
  [[nodiscard]] int stock_needed() const;
  // It has a body, legs and a CHIP.
  [[nodiscard]] bool complete() const;

  bool operator==(const RobotRecord &) const = default;
};

// One of a dealer's three SVs (0x24fc18): the robot and its picture.
struct LineupEntry {
  int robot{};
  int picture{};
};

// A module record (0x1c1f90, 0x2c bytes, 0x09274b).
struct ModuleRecord {
  std::string name;
  float weight{};               // +0xc, added to the robot's (0x075f93)
  int price{};                  // +0x28
  int armour{};                 // +8, added to the robot's (0x0bb44a)
  int sockets{1};               // +0x10: the socket holes it takes (0x0bb5d3)
  float critical_percent{};     // +0x14, added to the robot's (0x075e3d)
  float knockdown_resistance{}; // +0x18, added to the robot's (0x075e66)
  int laser_reduction{};        // +0x20: percent, 0..100 (0x0bb6e8)
  int active_frames{};          // +0x22: the O.C's active time, display frames
  int charge_frames{};          // +0x24: the O.C's charge time, display frames
};

// A paint (0x1c31f8) or emblem (0x1c34a8): its name, price and index (+4,
// its picture's place in the swatches, 0x0bb78a and 0x0bb7e8).
struct PriceEntry {
  std::string name;
  int price{};
  int index{};
};

// A boss's CHIP (data/tables/added_chips.json "boss_shop"): sold at `level`
// on the days one of `contests` is held, once the player has won it.
struct BossChip {
  int id{};
  int level{};
  std::vector<int> contests;
};

// What the extractor writes to tables/garage.json.
struct GarageTables {
  std::vector<RobotRecord> robots;
  std::vector<std::array<LineupEntry, 3>> dealer_lineups;
  std::vector<ModuleRecord> modules;
  std::vector<PriceEntry> paints;
  std::vector<PriceEntry> emblems;
  // Wild Machines' stock, each lineup chosen by a calendar day: CHIPs and
  // modules as (record, level) pairs, used SVs as robot records.
  std::vector<std::vector<RobotPart>> chip_lineups;
  std::vector<std::vector<RobotPart>> module_lineups;
  std::vector<std::vector<int>> used_sv_lineups;
  std::vector<BossChip> boss_chips; // the port's, joined by add_chip_lineups

  [[nodiscard]] const RobotRecord *robot(int index) const;
};

// Throws std::runtime_error naming what is malformed.
[[nodiscard]] GarageTables parse_garage_tables(const json::JsonValue &document);

// What a part, module, paint or emblem costs (0x0932c7); 0 for one missing.
[[nodiscard]] std::int64_t part_price(const PartTables &parts, const GarageTables &garage,
                                      PartKind kind, int id, int level);
// A SV's price at a dealer: all of its parts', modules', paint's and
// emblem's (0x0392ff).
[[nodiscard]] std::int64_t robot_price(const PartTables &parts, const GarageTables &garage,
                                       const RobotRecord &robot);

// The name a part's record gives it; empty for one missing.
[[nodiscard]] std::string part_name(const PartTables &parts, const GarageTables &garage,
                                    PartKind kind, int id);

// Adds each fitted module's critical and knockdown resistance to the CHIP's,
// with no cap, and sets the beam resistance from their laser reductions
// added, kept to 0..100 percent (0x075d4a); a missing module adds nothing.
void add_module_bonuses(CombatStats &combat, const GarageTables &garage,
                        const std::vector<int> &modules);

// Fits a SV into an empty hangar, adding each part and module to the stock
// (0x01da04): the CHIP's stock entry is named after its record. Returns the
// CHIP's stock index, or -1 when the stock is too full.
int install_robot(Garage &garage, int hangar, const RobotRecord &robot, const PartTables &parts,
                  const GarageTables &tables);

} // namespace phantomcrash
