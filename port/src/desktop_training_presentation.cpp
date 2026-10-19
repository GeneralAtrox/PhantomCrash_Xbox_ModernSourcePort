#include "desktop_training.hpp"
#include "desktop_online_presentation.hpp"
#include <algorithm>
#include <iterator>

namespace phantomcrash {
void TrainingScreen::refresh_marked_tags(const float seconds) {
  constexpr float refresh_seconds = 0.5F;
  marked_refresh_ -= seconds;
  if (live_world_ == nullptr || marked_refresh_ > 0.0F) {
    return;
  }
  marked_refresh_ = refresh_seconds;
  std::set<std::string> tags;
  for (const RosterStatus &entry : live_world_->roster_status(live_area_)) {
    if (entry.eliminated && !entry.tag.empty()) {
      tags.insert(entry.tag);
    }
  }
  for (SplitView &split : splits_) {
    split.hud->set_marked_tags(tags);
  }
  hud_->set_marked_tags(std::move(tags));
}

void TrainingScreen::tick_effects(const double tick_seconds) {
  const auto seconds = static_cast<float>(tick_seconds);
  hud_->tick(*battle_, seconds);
  refresh_marked_tags(seconds);
  for (SplitView &split : splits_) {
    split.hud->tick(*battle_, seconds);
  }
  if (chatter_) {
    chatter_->set_scene(scene_ && scene_->scene().active());
    chatter_->tick(*battle_, tick_seconds, message_ != nullptr || finale_);
    queue_dialogue(chatter_->take_sounds());
  }
  if (view_) {
    view_->tick(*battle_, seconds);
  }
  if (supply_) {
    supply_->tick(*battle_, effects_ ? &effects_->system() : nullptr);
  }
  if (shots_) {
    shots_->tick(*battle_, effects_->system(), seconds, view_.get());
  }
  if (point_lights_) {
    point_lights_->tick(*battle_, seconds);
  }
  if (mech_vfx_) {
    mech_vfx_->tick(*battle_, seconds);
  }
  if (effects_) {
    effects_->tick(seconds);
  }
}

void TrainingScreen::present_online() {
  if (!online_presentation_) {
    return;
  }
  online_presentation_->consume(*coop_match_, [&](const std::uint32_t) {
    battle_->select_player(player_index_);
    sync_coop_views();
    for (const auto &event : battle_->events()) {
      if (event.kind == BattleEventKind::damaged && event.robot == player_index_) {
        rumble_.push_back({0, event.amount});
      }
    }
    const auto consume_scene = [&](BattleScene *scene) {
      if (scene) {
        std::ranges::move(scene->take_sounds(), std::back_inserter(sounds_));
        if (auto music = scene->take_music()) {
          cue_music_ = std::move(*music);
        }
      }
    };
    consume_scene(coop_match_->entrances.get());
    consume_scene(coop_match_->rider_scenes[coop_slot_].get());
    gather_sounds(online::tick_seconds);
    tick_effects(online::tick_seconds);
  });
  // Historical event presentation can bind historical poses and looks. Drawing
  // continues from the current predicted simulation after the queue is drained.
  sync_coop_views();
  if (view_) {
    view_->bind(*battle_);
  }
}
} // namespace phantomcrash
