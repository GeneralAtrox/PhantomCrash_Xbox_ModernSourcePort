#include "desktop_training.hpp"
#include "desktop_coop_matches.hpp"

#include <algorithm>
#include <SDL3/SDL.h>

namespace phantomcrash {

void TrainingScreen::update_result(const double seconds) {
  if (online()) {
    play_seconds_ = coop_match_->players[coop_slot_].play_seconds;
    return; // Shared simulation owns completion; only committed settlement ends this view.
  }
  const bool death_pending = battle_ && battle_->player().dying;
  const bool death_finished = battle_ && !battle_->player().alive && !death_pending;
  if (!in_scene() && battle_ && battle_->player().alive) {
    play_seconds_ += seconds;
  }
  done_ = done_ || (!death_pending && ((free_play_ && screens_ == 1 && death_finished) ||
                                       (scene_ && scene_->scene().finished()) || finale_done() ||
                                       (!free_play_ && tutorial_.finished() && !message_)));
  if (!coop_match_ && battle_) {
    extra_prize_ += match_event_prize(battle_->events(), player_index_);
  }
  if (!match_results_ || !battle_) {
    return;
  }
  const bool won =
      coop_match_ ? coop_match_->players[coop_slot_].won : live_world_ && live_world_->player_won();
  // Only a ranker's defeat ends the battle (0x089d1b); after another
  // contest's champion it goes on, and leaving it later is the win (the
  // original grades champion wins up to 40 shootdowns).
  const bool win_ends = coop_match_ != nullptr || ranker_ != RankerContest::none;
  if (!battle_->player().alive) {
    outcome_ = MatchOutcome::retire;
    done_ = death_finished;
    if (done_) {
      SDL_Log("Result: player death presentation completed after %.3f seconds",
              battle_->player().death_seconds);
    }
  } else if (won && !win_ends) {
    outcome_ = MatchOutcome::win;
  } else if (won &&
             std::ranges::none_of(battle_->robots(), [](const Combatant &r) { return r.dying; }) &&
             !(scene_ && scene_->scene().defeat_holds())) {
    outcome_ = MatchOutcome::win;
    // The First Ranker's first clear stands a while first (0x089861);
    // a boss's defeat scene, which is that wait, has already stood it.
    victory_hold_ -= static_cast<float>(seconds);
    done_ = victory_hold_ <= 0.0F || (scene_ && scene_->scene().defeat_over());
  }
}

MatchSummary TrainingScreen::match_summary() const {
  if (online()) {
    if (!online_result_ready()) {
      throw std::runtime_error("Online result is not committed");
    }
    return coop_match_->settlements[coop_slot_]->summary;
  }
  battle_->select_player(player_index_);
  MatchSummary result;
  result.outcome = outcome_;
  result.shootdowns = battle_->player().kills;
  result.play_seconds = play_seconds_;
  result.lost = battle_->player().lost;
  const Combatant &player = battle_->player();
  result.armour = player.max_hp > 0.0F ? std::clamp(player.hp / player.max_hp, 0.0F, 1.0F) : 0.0F;
  const auto look = script_.robots.find("player");
  if (look != script_.robots.end()) {
    result.robot.parts[static_cast<int>(PartSlot::body)] =
        RobotPart{look->second.body, script_.battle.player.body_level};
    result.robot.parts[static_cast<int>(PartSlot::legs)] =
        RobotPart{look->second.legs, script_.battle.player.legs_level};
  }
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    const WeaponTuning *weapon = battle_->player().weapons[slot];
    if (weapon && weapon->part) {
      result.robot.parts[static_cast<int>(PartSlot::right_arm) + slot] =
          RobotPart{weapon->part->index, weapon->part->level};
    }
  }
  return result;
}

WorldFrame TrainingScreen::result_background() {
  battle_->select_player(player_index_);
  // Own the final poses and effects; returning the live battle cannot change them.
  return battle_frame(camera_, 640, 480, -1);
}

} // namespace phantomcrash
