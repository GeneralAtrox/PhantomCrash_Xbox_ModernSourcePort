#include "phantomcrash/sv_spec.hpp"
#include "phantomcrash/shop_pointer.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace phantomcrash {

SpecRobot hangar_spec(const Garage &owned, const int hangar) {
  SpecRobot robot;
  robot.garage = owned;
  robot.hangar = std::clamp(hangar, 0, garage_hangar_count - 1);
  robot.name = owned.hangars[static_cast<std::size_t>(robot.hangar)].name;
  return robot;
}

// The SV is fitted into hangar 0 of an empty stock, as buying it would.
SpecRobot record_spec(const RobotRecord &record, const PartTables &parts,
                      const GarageTables &garage) {
  SpecRobot robot;
  robot.garage.hangars[0].owned = true;
  install_robot(robot.garage, 0, record, parts, garage);
  robot.garage.hangars[0].name = record.name;
  robot.name = record.name;
  return robot;
}

SvSpec::SvSpec(SvSpecScript script, SpecRobot robot, const PartTables &parts,
               const GarageTables &garage, const PartStatsTable &table, std::string exit_to)
    : TownLogic(script), script_(std::move(script)), robot_(std::move(robot)), parts_(parts),
      garage_(garage), exit_to_(std::move(exit_to)) {
  totals_ = robot_totals(table, parts_, garage_, robot_.garage, fitted());
  const RobotLoadout loadout = hangar_loadout(robot_.garage, fitted());
  speeds_ = legs_speeds(parts_, loadout, load_stats(parts_, loadout));
  // The fight's stats as a battle builds them: the CHIP's and the legs',
  // then the fitted modules' added.
  combat_ =
      combat_stats(parts_, loadout.chip, loadout.chip_level, loadout.legs, loadout.legs_level);
  std::vector<int> modules;
  for (const int stock : fitted().modules) {
    if (const auto entry = robot_.garage.stock.find(stock); entry != robot_.garage.stock.end()) {
      modules.push_back(entry->second.id);
    }
  }
  add_module_bonuses(combat_, garage_, modules);
  if (const ChipRecord *chip = parts_.chip(loadout.chip)) {
    chip_ = chip_stats(*chip, loadout.chip_level);
  }
}

const Hangar &SvSpec::fitted() const {
  return robot_.garage.hangars[static_cast<std::size_t>(robot_.hangar)];
}

// 0x03bf07 and 0x03bfc0: left and right turn the page (sys 9), cancel
// closes (sys 8).
void SvSpec::update(const double tick_seconds, const InputFrame &raw_input) {
  InputFrame input = raw_input;
  if (shop_clicked(input, script_.close_hint)) {
    input.pressed |= script_.cancel_buttons;
  }
  for (const auto &arrow : script_.arrows) {
    if (input.clicked && shop_hit(input, arrow)) {
      input.pressed |= script_.right;
    }
  }
  if (input.mouse_wheel != 0) {
    input.pressed |= script_.right;
  }
  if (done()) {
    return;
  }
  help_seconds_ += tick_seconds;
  if (!advance(tick_seconds)) {
    return;
  }
  const int pages = static_cast<int>(script_.pages.size());
  const bool back = input.any_pressed(script_.left);
  if (pages > 1 && (back || input.any_pressed(script_.right))) {
    page_ = (page_ + (back ? pages - 1 : 1)) % pages;
    help_seconds_ = 0.0;
    sound(script_.page_sound);
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    sound(script_.back_sound);
    leave(exit_to_);
  }
}

double SvSpec::value(const SpecStat stat) const {
  switch (stat) {
  case SpecStat::armour:
    return totals_.armour;
  case SpecStat::walk:
    return speeds_.walk;
  case SpecStat::dash:
    return speeds_.dash;
  case SpecStat::jump:
    return speeds_.jump;
  case SpecStat::overclock:
    return totals_.active_seconds;
  case SpecStat::weight:
    return totals_.weight;
  case SpecStat::capacity:
    return totals_.capacity;
  case SpecStat::sockets:
    return totals_.sockets;
  }
  return 0.0;
}

double SvSpec::value(const CombatStat stat) const {
  constexpr double percent = 100.0;
  switch (stat) {
  case CombatStat::mortal_shot:
    return combat_.critical_percent;
  case CombatStat::repulsion_shield:
    return combat_.dodge_chance * percent;
  case CombatStat::knockdown_resistance:
    return combat_.knockdown_resistance;
  case CombatStat::knockback_resistance:
    return combat_.knockback_resistance;
  case CombatStat::laser_reduction:
    return totals_.laser_reduction;
  case CombatStat::charge_time:
    return std::max(totals_.charge_seconds, 0.0);
  case CombatStat::lock_time:
    return chip_.lock_frames / part_frames_per_second;
  case CombatStat::aim_speed:
    return chip_.track;
  }
  return 0.0;
}

// 0x03b82e: the arm weapons' names (0x0926be) and the backpack weapons'
// (0x0926ee).
std::optional<std::string> SvSpec::weapon(const PartSlot slot) const {
  const auto entry = robot_.garage.stock.find(fitted().part(slot));
  if (entry == robot_.garage.stock.end()) {
    return std::nullopt;
  }
  std::string name = part_name(parts_, garage_, entry->second.kind, entry->second.id);
  if (name.empty()) {
    return std::nullopt;
  }
  return name;
}

// 0x03ba94: the body record's maker (+4).
int SvSpec::maker() const {
  const auto entry = robot_.garage.stock.find(fitted().part(PartSlot::body));
  if (entry == robot_.garage.stock.end() || entry->second.id < 0 ||
      entry->second.id >= static_cast<int>(parts_.bodies.size())) {
    return -1;
  }
  return parts_.bodies[static_cast<std::size_t>(entry->second.id)].maker;
}

} // namespace phantomcrash
