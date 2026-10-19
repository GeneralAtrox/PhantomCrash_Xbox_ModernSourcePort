#pragma once

#include "phantomcrash/garage.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/part_stats.hpp"
#include "phantomcrash/robot_load.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/robot_totals.hpp"
#include "phantomcrash/sv_spec_script.hpp"
#include "phantomcrash/town_logic.hpp"

#include <optional>
#include <string>

namespace phantomcrash {

// The robot SV SPEC shows (the original copies it to + 0x10f5c): a hangar
// of the player's, or a SV on offer fitted into a stock of its own.
struct SpecRobot {
  Garage garage;
  int hangar{};
  std::string name;
};

[[nodiscard]] SpecRobot hangar_spec(const Garage &owned, int hangar);
[[nodiscard]] SpecRobot record_spec(const RobotRecord &robot, const PartTables &parts,
                                    const GarageTables &garage);

// SV SPEC (scene 36): the vehicle's page and the weapons' page, and the
// port's combat page; right turns to the next, left to the one before;
// cancel goes back to where it was opened from.
class SvSpec final : public TownLogic {
public:
  SvSpec(SvSpecScript script, SpecRobot robot, const PartTables &parts, const GarageTables &garage,
         const PartStatsTable &table, std::string exit_to);

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] const SvSpecScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] const SpecRobot &robot() const noexcept {
    return robot_;
  }
  [[nodiscard]] const RobotTotals &totals() const noexcept {
    return totals_;
  }
  // The page shown, an index into the script's pages.
  [[nodiscard]] int page() const noexcept {
    return page_;
  }
  [[nodiscard]] SpecPageKind page_kind() const {
    return script_.pages[static_cast<std::size_t>(page_)].kind;
  }
  [[nodiscard]] const std::string &help_text() const {
    return script_.pages[static_cast<std::size_t>(page_)].help;
  }
  [[nodiscard]] double help_seconds() const noexcept {
    return help_seconds_;
  }

  // A vehicle line's value, in the units it shows.
  [[nodiscard]] double value(SpecStat stat) const;
  // A combat line's value, as the fight takes it (CombatStats).
  [[nodiscard]] double value(CombatStat stat) const;
  // The weapon in a slot's name; none when the slot is empty.
  [[nodiscard]] std::optional<std::string> weapon(PartSlot slot) const;
  // The body's maker, whose badge shows; -1 without a body.
  [[nodiscard]] int maker() const;

private:
  [[nodiscard]] const Hangar &fitted() const;

  SvSpecScript script_;
  SpecRobot robot_;
  const PartTables &parts_;
  const GarageTables &garage_;
  std::string exit_to_;
  RobotTotals totals_;
  LegsSpeeds speeds_;
  CombatStats combat_;
  ChipStats chip_;
  int page_{};
  double help_seconds_{};
};

} // namespace phantomcrash
