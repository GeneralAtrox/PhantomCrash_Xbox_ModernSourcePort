#include "desktop_training.hpp"
#include "desktop_coop_matches.hpp"
#include <algorithm>

namespace phantomcrash {

void TrainingScreen::sync_coop_views() {
  if (!view_ || (coop_revision_ == coop_match_->look_revision &&
                 live_changes_ == coop_match_->world->changes(live_area_))) {
    return;
  }
  coop_revision_ = coop_match_->look_revision;
  live_changes_ = coop_match_->world->changes(live_area_);
  for (std::size_t index = 0; index < battle_->robots().size(); ++index) {
    const auto look = coop_match_->look(static_cast<int>(index));
    if (index >= coop_looks_.size()) {
      coop_looks_.push_back(look);
      view_->add(look);
    } else if (coop_looks_[index] != look) {
      coop_looks_[index] = look;
      view_->replace(index, look);
    }
    if (!online()) {
      battle_->set_radius(static_cast<int>(index),
                          view_->radius(index, script_.battle.settings.robot_radius));
    }
  }
  view_->bind(*battle_);
}

bool TrainingScreen::coop_contest_won() const {
  if (online_result_ready()) {
    return coop_match_->settlements[coop_slot_]->won;
  }
  return coop_match_ && coop_match_->players[coop_slot_].won;
}

bool TrainingScreen::online_result_ready() const {
  return online() && coop_match_->settlements[coop_slot_] &&
         coop_match_->settlements[coop_slot_]->ready;
}

std::shared_ptr<void> TrainingScreen::result_resources() const {
  return coop_match_;
}

std::int64_t TrainingScreen::settle_coop(QuestSave &save) {
  if (online()) {
    if (!online_result_ready()) {
      throw std::runtime_error("Cannot settle an uncommitted online result");
    }
    if (online_settled_) {
      return 0;
    }
    const auto &result = *coop_match_->settlements[coop_slot_];
    for (const int beaten : result.beaten) {
      if (std::ranges::find(save.beaten, beaten) == save.beaten.end()) {
        save.beaten.push_back(beaten);
      }
    }
    online_settled_ = true;
    return result.bounty;
  }
  return coop_match_ ? coop_match_->leave(coop_slot_, save) : 0;
}

} // namespace phantomcrash
