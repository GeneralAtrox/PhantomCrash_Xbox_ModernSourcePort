// A boss's defeat on the training screen (battle_scene_defeat.cpp): the
// shot-down boss that starts it, its ranker's line and the camera on it.

#include "desktop_training.hpp"

namespace phantomcrash {

namespace {

// 0x07675e: every robot that hurt the victim shares its shooting down; a
// player's among them puts a boss in its defeat.
bool shot_by_player(const Battle &battle, const Combatant &victim) {
  const auto &robots = battle.robots();
  const auto piloted = [&](const std::size_t index) {
    return index < robots.size() && robots[index].piloted;
  };
  if (victim.shot_by >= 0 && piloted(static_cast<std::size_t>(victim.shot_by))) {
    return true;
  }
  for (std::size_t index = 0; index < victim.damage_from.size(); ++index) {
    if (victim.damage_from[index] > 0.0F && piloted(index)) {
      return true;
    }
  }
  return false;
}

} // namespace

void TrainingScreen::observe_defeat() {
  BattleScene &scene = scene_->scene();
  if (scene.defeat_robot() >= 0) {
    scene.set_defeat_line(chatter_ && chatter_->speaking());
    return;
  }
  const auto &robots = battle_->robots();
  for (const BattleEvent &event : battle_->events()) {
    if (event.kind != BattleEventKind::destroyed || event.robot < 0 ||
        event.robot >= static_cast<int>(robots.size())) {
      continue;
    }
    // A robot of the other side (0x082c40) with a boss's loadout (0x082cff).
    const Combatant &boss = robots[static_cast<std::size_t>(event.robot)];
    if (!boss.rival || !boss.boss || !shot_by_player(*battle_, boss)) {
      continue;
    }
    const SceneTuning &tuning = script_.battle.scenes;
    const float frames =
        ranker_ == RankerContest::first ? tuning.first_ranker_defeat_frames : tuning.defeat_frames;
    scene.start_defeat(event.robot, frames, ranker_ != RankerContest::none);
    // An Area Ranker's states 0x14-0x16 speak the area's line (0x091f29).
    if (ranker_ == RankerContest::area && chatter_) {
      scene.set_defeat_line(chatter_->say_defeat(event.robot, ranker_area_));
    }
    defeat_chase_.reset();
    defeat_views_.reset();
    return;
  }
}

// 0x089bb9 points the camera at the boss each tick through 0x07e538: the
// stage's fixed cameras (view 5, 0x07f641) with the eye brought within
// defeat_watch of it, as the result's backdrop does.
std::optional<CameraShot> TrainingScreen::defeat_shot(const float seconds, const SightTest &sees) {
  const int boss = scene_ ? scene_->scene().defeat_robot() : -1;
  if (boss < 0 || boss >= static_cast<int>(battle_->robots().size())) {
    return std::nullopt;
  }
  const CameraSubject subject = camera_subject(static_cast<std::size_t>(boss));
  const bool snap = !defeat_views_;
  if (snap) {
    CameraViewSettings settings = script_.battle.views;
    settings.start_view = camera_view::fixed;
    defeat_views_.emplace(settings, views_.fixed_points());
    defeat_chase_.emplace(script_.battle.camera);
  }
  // A stage without fixed cameras falls back to the view behind the boss.
  defeat_chase_->tick(subject, battle_->stage(), seconds, snap);
  defeat_views_->tick(subject, defeat_chase_->chase(), seconds, sees, snap);
  CameraShot shot = defeat_views_->shot();
  const Vec3 away = shot.eye - shot.target;
  const float watch = script_.battle.scenes.defeat_watch;
  if (const float distance = length(away); distance > watch) {
    shot.eye = shot.target + away * (watch / distance);
  }
  return shot;
}

} // namespace phantomcrash
