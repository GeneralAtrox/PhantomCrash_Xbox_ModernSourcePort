#include "desktop_battle_view.hpp"
#include "desktop_wreck_collision.hpp"
#include "phantomcrash/money.hpp"

namespace phantomcrash {

void BattleView::sync_wrecks(Battle &battle) {
  const auto &wrecks = battle.wrecks().items();
  if (wreck_battle_ != &battle) {
    clear_wrecks();
    wreck_battle_ = &battle;
    wreck_generation_ = battle.restore_generation();
  } else if (wreck_generation_ != battle.restore_generation()) {
    // Online rollback and confirmed playback restore the battle several times
    // a tick. Wrecks are only appended, so keep every posed wreck up to the
    // first one the restored state placed differently; rebuilding them all
    // on each restore made every kill slow the match further.
    wreck_generation_ = battle.restore_generation();
    // Offline view-built collision lives in the battle: rebuild a wreck the
    // restored state carries without it.
    const bool simulated = battle.wrecks().simulation_collision();
    std::size_t keep = 0;
    while (keep < wreck_items_.size() && keep < wrecks.size() &&
           wreck_roots_[keep].m == wrecks[keep].root.m && (simulated || wrecks[keep].collision)) {
      wreck_solid_[keep] = wrecks[keep].solid();
      ++keep;
    }
    wreck_items_.resize(keep);
    wreck_roots_.resize(keep);
    wreck_solid_.resize(keep);
  }
  while (wreck_items_.size() < wrecks.size()) {
    const std::size_t index = wreck_items_.size();
    const auto &wreck = wrecks[index];
    auto &items = wreck_items_.emplace_back();
    wreck_roots_.push_back(wreck.root);
    wreck_solid_.push_back(false);
    RobotVisual visual(world_, wreck.assembly);
    visual.add_items(wreck.pose, wreck.root, items, &wreck.shoulders, wreck.lost);
    if (!battle.wrecks().simulation_collision()) {
      battle.wrecks().set_collision(index, posed_collision(world_, items));
    }
    SDL_Log("Robot wreck: created=%zu parts=%zu triangles=%zu burn_left=%.3f reward=%d NY", index,
            items.size(), wreck.collision ? wreck.collision->walls().size() : 0, wreck.burn_left,
            battle.settings().robot_wrecks.reward_ny);
  }
  for (const auto &event : battle.events()) {
    if (event.kind == BattleEventKind::wreck_destroyed) {
      SDL_Log("Robot wreck: destroyed by=%d reward=%.0f NY (no shootdown)", event.robot,
              event.amount * ny_per_stored_unit);
    }
  }
  for (std::size_t i = 0; i < wrecks.size(); ++i) {
    if (wrecks[i].solid() && !wreck_solid_[i]) {
      SDL_Log("Robot wreck: solid=%zu fire extinguished, persistent collision enabled", i);
      wreck_solid_[i] = true;
    }
  }
}

void BattleView::clear_wrecks() {
  wreck_items_.clear();
  wreck_roots_.clear();
  wreck_solid_.clear();
  wreck_battle_ = nullptr;
  wreck_generation_ = 0;
}

void BattleView::add_wreck_items(const Battle &battle, std::vector<WorldItem> &items) const {
  const auto &wrecks = battle.wrecks().items();
  for (std::size_t i = 0; i < wreck_items_.size() && i < wrecks.size(); ++i) {
    if (!wrecks[i].removed) {
      items.insert(items.end(), wreck_items_[i].begin(), wreck_items_[i].end());
    }
  }
}
} // namespace phantomcrash
