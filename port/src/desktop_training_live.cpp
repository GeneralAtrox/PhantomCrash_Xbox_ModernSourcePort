// The live RUMBLING's battle on the training screen: the world lends its
// area's battle (rumbling_world.hpp), the player's robot takes robot 0's
// place, and the opponents are drawn as the ones brought in.

#include "desktop_training.hpp"

namespace phantomcrash {

void TrainingScreen::seat_player(const BattleSettings &settings, const float hp) {
  const TrainingBattle &setup = script_.battle;
  std::array<std::string, weapon_slots> weapons{};
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    const auto found = settings.weapons.find(setup.player.weapons[slot]);
    if (found != settings.weapons.end()) {
      auto tuning = found->second;
      if (coop_match_) {
        tuning.name = "coop_" + std::to_string(coop_slot_) + "_" + std::to_string(slot);
      }
      weapons[slot] = battle_->add_weapon(std::move(tuning)).name;
    }
  }
  battle_->set_identity(player_index_, "player", 0, hp);
  battle_->arm(player_index_, weapons);
  battle_->bring_in(player_index_);
  battle_->place(player_index_, setup.player_start, setup.player_heading);
}

void TrainingScreen::add_live_views() {
  live_changes_ = live_world_->changes(live_area_);
  live_entrants_.assign(battle_->robots().size(), -1);
  for (std::size_t index = 1; index < battle_->robots().size(); ++index) {
    const RumblingRobot *robot = live_world_->robot(live_area_, static_cast<int>(index));
    live_entrants_[index] = robot != nullptr ? robot->entrant : -1;
    try {
      view_->add(robot != nullptr ? robot->look : RobotParts{});
    } catch (const std::exception &) {
      view_->add(RobotParts{}); // a part out of range: still drawn, as a stand-in
    }
  }
  view_->preload(live_world_->lent_looks());
}

void TrainingScreen::follow_live() {
  if (coop_match_) {
    sync_coop_views();
    return;
  }
  if (live_world_ == nullptr || !view_ || live_world_->changes(live_area_) == live_changes_) {
    return;
  }
  live_changes_ = live_world_->changes(live_area_);
  const float fallback = script_.battle.settings.robot_radius;
  for (std::size_t index = 1; index < live_entrants_.size() && index < view_->size(); ++index) {
    const RumblingRobot *robot = live_world_->robot(live_area_, static_cast<int>(index));
    if (robot == nullptr || robot->entrant == live_entrants_[index]) {
      continue;
    }
    live_entrants_[index] = robot->entrant;
    try {
      view_->replace(index, robot->look);
    } catch (const std::exception &) {
      view_->replace(index, RobotParts{}); // as add_live_views
    }
    battle_->set_radius(static_cast<int>(index), view_->radius(index, fallback));
  }
}

} // namespace phantomcrash
