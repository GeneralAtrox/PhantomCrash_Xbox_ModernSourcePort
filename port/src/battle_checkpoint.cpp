#include "phantomcrash/battle.hpp"
#include "phantomcrash/robot_combat.hpp"
#include <chrono>
#include <stdexcept>

namespace phantomcrash {

std::unique_ptr<Battle> Battle::clone_state() const {
  auto copy = std::unique_ptr<Battle>(new Battle(*this));
  std::map<const WeaponTuning *, const WeaponTuning *> weapons;
  for (const auto &[name, weapon] : settings_.weapons) {
    weapons.emplace(&weapon, &copy->settings_.weapons.at(name));
  }
  const auto rebind = [&](const WeaponTuning *&weapon) {
    if (weapon) {
      const auto found = weapons.find(weapon);
      if (found == weapons.end()) {
        throw std::runtime_error("Battle checkpoint contains an unowned weapon");
      }
      weapon = found->second;
    }
  };
  for (auto &robot : copy->robots_) {
    for (auto &weapon : robot.weapons) {
      rebind(weapon);
    }
    if (robot.pose) {
      const auto previous = robot.pose;
      robot.pose = std::make_shared<RobotCombat>(*robot.pose);
      if (robot.mover.timings_ == &previous->assembly().timings) {
        robot.mover.timings_ = &robot.pose->assembly().timings;
      }
    }
  }
  for (auto &shot : copy->armory_.projectiles_) {
    rebind(shot.weapon);
  }
  for (auto &beam : copy->armory_.beams_) {
    rebind(beam.weapon);
  }
  for (auto &blast : copy->armory_.blasts_) {
    rebind(blast.weapon);
  }
  for (auto &landing : copy->armory_.landings_) {
    rebind(landing.weapon);
  }
  for (auto &event : copy->events_) {
    rebind(event.weapon);
  }
  if (supply_ && supply_->route_) {
    copy->supply_->route_ = nullptr;
    for (std::size_t route = 0; route < supply_->settings_.routes.size(); ++route) {
      if (&supply_->settings_.routes[route] == supply_->route_) {
        copy->supply_->route_ = &copy->supply_->settings_.routes[route];
        break;
      }
    }
    if (!copy->supply_->route_) {
      throw std::runtime_error("Battle checkpoint contains an unowned supply route");
    }
  }
  return copy;
}

std::shared_ptr<const Battle> Battle::checkpoint() const {
  return clone_state();
}

void Battle::restore_checkpoint(const Battle &checkpoint) {
  // Finish allocating/rebinding before touching the live battle. Moving maps
  // and vectors transfers the storage referenced by the newly rebound pointers.
  auto restored = checkpoint.clone_state();
  const auto generation = restore_generation_ + 1;
  *this = std::move(*restored);
  restore_generation_ = generation;
}

void Battle::prepare_deterministic() {
  if (!nav_ && nav_future_.valid()) {
    if (nav_future_.wait_for(std::chrono::seconds(30)) != std::future_status::ready) {
      throw std::runtime_error("Online navigation preparation timed out");
    }
    static_cast<void>(nav_graph());
  }
}

} // namespace phantomcrash
