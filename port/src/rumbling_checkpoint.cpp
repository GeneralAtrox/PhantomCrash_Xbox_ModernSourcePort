#include "phantomcrash/rumbling_world.hpp"
#include <stdexcept>

namespace phantomcrash {

struct RumblingWorld::Checkpoint {
  const RumblingWorld *owner{};
  std::minstd_rand random;
  std::int64_t epoch{};
  std::array<Area, roster_areas> areas;
  std::array<std::shared_ptr<const Battle>, roster_areas> battles;
  int lent{-1};
  bool background{};
  bool deterministic{};
  std::vector<RobotParts> lent_looks;
};

std::shared_ptr<const RumblingWorld::Checkpoint> RumblingWorld::checkpoint() const {
  auto state = std::make_shared<Checkpoint>();
  state->owner = this;
  state->random = random_;
  state->epoch = epoch_;
  state->areas = areas_;
  state->lent = lent_;
  state->background = background_;
  state->deterministic = deterministic_;
  state->lent_looks = lent_looks_;
  for (std::size_t i = 0; i < areas_.size(); ++i) {
    const Battle *battle = areas_[i].battle.get();
    if (!battle) {
      continue;
    }
    // The lent battle changes outside the world, so it is always copied.
    SavedBattle &saved = saved_battles_[i];
    if (static_cast<int>(i) == lent_ || saved.of != battle || !saved.copy) {
      saved = {battle, battle->checkpoint()};
    }
    state->battles[i] = saved.copy;
    if (static_cast<int>(i) == lent_) {
      saved = {};
    }
  }
  return state;
}

void RumblingWorld::restore_checkpoint(const Checkpoint &state) {
  if (state.owner != this) {
    throw std::runtime_error("Cannot restore a checkpoint into another rumbling world");
  }
  auto areas = state.areas;
  for (std::size_t i = 0; i < areas.size(); ++i) {
    SavedBattle &saved = saved_battles_[i];
    if (!state.battles[i]) {
      saved = {};
      continue;
    }
    // An idle battle the world has not changed since it was this copy is
    // already in that state.
    Battle *battle = areas[i].battle.get();
    if (static_cast<int>(i) != state.lent && saved.of == battle && saved.copy == state.battles[i]) {
      continue;
    }
    // Preserve the Battle identity shared with training screens and the match.
    battle->restore_checkpoint(*state.battles[i]);
    saved =
        static_cast<int>(i) == state.lent ? SavedBattle{} : SavedBattle{battle, state.battles[i]};
  }
  random_ = state.random;
  epoch_ = state.epoch;
  areas_ = std::move(areas);
  lent_ = state.lent;
  background_ = state.background;
  deterministic_ = state.deterministic;
  lent_looks_ = state.lent_looks;
}

void RumblingWorld::prepare_deterministic() {
  for (auto &area : areas_) {
    if (area.battle) {
      area.battle->prepare_deterministic();
      saved_battles_.fill({});
    }
  }
  deterministic_ = true;
}

} // namespace phantomcrash
