#include "desktop_training.hpp"
#include "desktop_coop_matches.hpp"
#include "desktop_training_vs.hpp"
#include <algorithm>
#include <iterator>

namespace phantomcrash {
namespace {
constexpr unsigned all_weapons = (1U << weapon_slots) - 1;
constexpr int optic_mode = 2;
} // namespace

void TrainingScreen::tick_battle(const InputFrame &input, const float seconds) {
  const LessonStep *step = tutorial_.step();
  const bool practising = free_play_ || tutorial_.practising();
  // A scene walks the robot, or a boss's entrance holds it (0x082e60).
  const bool scripted = in_scene();
  const bool locked =
      message_ != nullptr || !practising || scripted || paused_ || done_ || online_exit_requested_;
  RobotControl control;
  FireControl fire;
  if (!locked) {
    // The tutorial's lessons keep BEGINNER MODE off (0x0817a8).
    control = battle_control(view_changed(input, controls_), buttons_, free_play_ ? nullptr : step,
                             standard_controls_, beginner_ && free_play_);
    fire = battle_fire(input, buttons_);
    fire.camouflage = fire.camouflage && (free_play_ || mode_ == optic_mode);
  }
  if (!jump_armed_) {
    jump_armed_ = (input.held & buttons_.jump) == 0;
    control.jump = false;
  }
  if (!online()) {
    battle_->set_drones_active(free_play_ || (practising && step != nullptr && step->drones));
    battle_->set_allowed_weapons(free_play_                      ? all_weapons
                                 : practising && step != nullptr ? step->weapons
                                                                 : 0U);
    if (tutorial_.take_place_drone() && step != nullptr && battle_->robots().size() > 1) {
      battle_->place_facing_player(1, step->place_drone);
    }
  }
  const float heading_before = battle_->player().mover.heading();
  if (view_) {
    view_->bind(*battle_);
  }
  if (scene_ && !online()) {
    scene_->scene().prepare(*battle_, seconds);
  }
  if (vs_) {
    vs_controls(input, locked);
  }
  if (online()) {
    capture_online(control, fire);
  } else if (coop_match_) {
    battle_->set_player_control(player_index_, control, fire);
  } else {
    battle_->tick(control, fire, seconds);
  }
  for (const BattleEvent &event : battle_->events()) {
    if (!online() && event.kind == BattleEventKind::damaged && event.robot == player_index_) {
      rumble_.push_back({0, event.amount});
    }
  }
  if (scene_ && !online()) {
    scene_->scene().observe(*battle_);
    observe_defeat();
    std::ranges::move(scene_->scene().take_sounds(), std::back_inserter(sounds_));
    if (auto music = scene_->scene().take_music()) {
      cue_music_ = std::move(*music);
    }
  }
  if (!online()) {
    gather_sounds(seconds);
  }
  tutorial_.update(lesson_sample(battle_->player(), heading_before, seconds));
}

} // namespace phantomcrash
