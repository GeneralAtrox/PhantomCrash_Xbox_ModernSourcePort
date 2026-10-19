#include "phantomcrash/battle.hpp"
#include "phantomcrash/battle_chatter.hpp"
#include "phantomcrash/money.hpp"

namespace phantomcrash {

int Battle::object_count() const {
  return objects_ ? static_cast<int>(objects_->objects().size()) : 0;
}

void Battle::add_object_targets(std::vector<Target> &out) const {
  if (!objects_) {
    return;
  }
  for (const auto &object : objects_->objects()) {
    // Keep a slot even after destruction: outstanding projectiles and hits
    // retain their target indices, including the supply targets that follow.
    out.push_back({object.centre, object.radius, -1, object.solid(), &object.collision, false});
  }
}

bool Battle::hit_object(const WeaponHit &hit) {
  const int index = hit.victim - static_cast<int>(robots_.size());
  if (index < 0 || index >= object_count()) {
    return false;
  }
  if (const auto broken = objects_->hit(static_cast<std::size_t>(index))) {
    BattleEvent event{hit.attacker, BattleEventKind::effect};
    event.slot = broken->effect;
    event.at = broken->at;
    event.object = index;
    events_.push_back(event);
    // 0x047ed9: a shot that starts an object's destruction has its robot
    // remark on it, unless the object is of type 5, 6 or 8.
    const bool remarks = broken->kind != 5 && broken->kind != 6 && broken->kind != 8;
    const bool robot = hit.attacker >= 0 && hit.attacker < static_cast<int>(robots_.size());
    if (remarks && robot) {
      chatter(robots_[static_cast<std::size_t>(hit.attacker)], chatter_event::object_broken);
    }
    // 0x0486af: the shot pays its robot by the object's kind; a blast that
    // catches the object on its way does not (the overlap loop of 0x047ed9).
    const auto &rewards = settings_.object_reward_ny;
    const bool direct = hit.directional || hit.direct;
    if (robot && direct && broken->kind >= 0 && broken->kind < static_cast<int>(rewards.size()) &&
        rewards[static_cast<std::size_t>(broken->kind)] > 0) {
      BattleEvent reward{hit.attacker, BattleEventKind::object_reward};
      reward.amount =
          static_cast<float>(rewards[static_cast<std::size_t>(broken->kind)] / ny_per_stored_unit);
      reward.at = broken->at;
      events_.push_back(reward);
    }
  }
  return true;
}

} // namespace phantomcrash
