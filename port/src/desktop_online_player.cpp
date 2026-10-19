#include "desktop_online_factory.hpp"
#include "desktop_training_robots.hpp"
#include "phantomcrash/robot_combat.hpp"

namespace phantomcrash {

void prepare_online_player(CoopMatch &match, const int slot, const online::BattlePlayer &player,
                           const TrainingScript &script, OnlineBattleResources &resources) {
  auto &battle = *match.battle;
  const auto &setup = script.battle;
  const int actor = match.join(slot, script.robots.at("player"));
  battle.select_player(actor);
  auto settings = setup.settings;
  static_cast<void>(apply_weapon_records(settings.weapons, *resources.parts));
  std::array<std::string, weapon_slots> weapons;
  for (std::size_t weapon = 0; weapon < weapon_slots; ++weapon) {
    const auto found = settings.weapons.find(setup.player.weapons[weapon]);
    if (found != settings.weapons.end()) {
      auto tuning = found->second;
      tuning.name = "coop_" + std::to_string(slot) + "_" + std::to_string(weapon);
      weapons[weapon] = battle.add_weapon(std::move(tuning)).name;
    }
  }
  const float hp = robot_hp(script, resources.parts, "player", setup.player, true);
  battle.set_identity(actor, "player", 0, hp);
  battle.arm(actor, weapons);
  battle.bring_in(actor);
  battle.place(actor, setup.player_start, setup.player_heading);
  battle.set_tag(actor, player.profile.player_name);
  const auto garage =
      setup.player.modules.empty() ? std::optional<GarageTables>{} : resources.garage;
  battle.set_combat(actor, robot_combat(script, resources.parts, garage, "player", setup.player));
  if (const auto *chip = resources.parts->chip(setup.player.chip)) {
    const auto stats = chip_stats(*chip, setup.player.chip_level);
    battle.set_lock_seconds(actor, stats.lock_frames / part_frames_per_second);
    battle.set_sight(actor, sight_boxes(resources.hud, stats));
  }
  const auto assembly = assemble_robot(resources.robots.catalogue(), resources.robots.skeletons(),
                                       script.robots.at("player"));
  auto movement = setup.stride_speeds ? stride_tuning(resources.robots, assembly, settings.movement)
                                      : settings.movement;
  const auto loadout = training_loadout(script, "player", setup.player);
  movement =
      loaded_movement(*resources.parts, loadout, load_stats(*resources.parts, loadout), movement);
  battle.set_movement(actor, movement);
}
} // namespace phantomcrash
