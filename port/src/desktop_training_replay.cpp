#include "desktop_training.hpp"
#include <algorithm>

namespace phantomcrash {
void TrainingScreen::start_recording(const TrainingContext &context) {
  if (!match_results_) {
    return;
  }
  try {
    replay_ = std::make_shared<ReplayArchive>();
    replay_->roots = &context.roots;
    replay_->player_name = context.player_name;
    replay_->views = script_.battle.views;
    replay_->chase = script_.battle.camera;
    if (auto file = context.roots.find_asset(script_.battle.scenes.tables)) {
      replay_->fixed_points =
          parse_fixed_cameras(json::read_file(*file), script_.battle.scenes.stage);
    }
  } catch (const std::exception &error) {
    SDL_Log("Replay recording unavailable: %s", error.what());
    replay_.reset();
  }
}
void TrainingScreen::begin_recording() {
  recording_frame_ = false;
  if (!replay_ || paused_) {
    return;
  }
  if (!replay_->accepts(clock_)) {
    // As append would drop this frame: its sounds go too.
    static_cast<void>(replay_->take_sounds());
    return;
  }
  recording_frame_ = true;
  replay_frame_ = {};
  replay_frame_.seconds = clock_;
  replay_frame_.music = heard_music_.empty() ? music() : heard_music_;
  replay_frame_.sounds = replay_->take_sounds();
  textures_.record_into(&replay_frame_.overlay);
}
void TrainingScreen::record_world(const WorldFrame &frame,
                                  const std::optional<WorldFrame> &monitor) {
  if (!replay_ || paused_ || !recording_frame_) {
    return;
  }
  replay_frame_.player = frame;
  if (replay_->size() == 0) {
    replay_->collision = battle_->stage();
  }
  replay_frame_.monitor = monitor;
  if (in_cockpit() && view_) {
    view_->add_items(*battle_, replay_frame_.hidden_player, -1);
    std::erase_if(replay_frame_.hidden_player,
                  [this](const WorldItem &item) { return item.robot != player_index_; });
  }
  for (std::size_t i = 0; i < battle_->robots().size(); ++i) {
    ReplaySubject subject = replay_subject(i);
    if (i == 0 && !replay_->player_name.empty()) {
      subject.name = replay_->player_name;
    }
    replay_frame_.subjects.push_back(std::move(subject));
  }
}

ReplaySubject TrainingScreen::replay_subject(const std::size_t index) const {
  const auto &robot = battle_->robots()[index];
  ReplaySubject subject;
  subject.alive = robot.alive || robot.dying; // keep the death pose selectable in replay
  subject.in_play = robot.mover.mode() != MoveMode::scripted &&
                    battle_->stage().in_play_area(robot.mover.position());
  subject.name = robot.tag.empty() ? robot.name : robot.tag;
  const auto velocity = robot.mover.velocity();
  subject.cockpit.vertical_speed = velocity.y;
  subject.cockpit.gravity = robot.mover.tuning().gravity;
  const float dash = robot.mover.tuning().dash_speed;
  subject.cockpit.speed_ratio = dash > 0 ? std::hypot(velocity.x, velocity.z) / dash : 0;
  subject.cockpit.armour = robot.max_hp > 0 ? robot.shown_armour.value() / robot.max_hp : 0;
  subject.cockpit.filling = robot.shown_armour.filling();
  subject.cockpit.camouflage = robot.camouflage;
  subject.cockpit.camouflaged = robot.camouflaged;
  subject.camera = camera_subject(index);
  return subject;
}
void TrainingScreen::finish_recording() {
  textures_.record_into(nullptr);
  if (!replay_ || paused_ || !recording_frame_ || replay_frame_.player.width <= 0) {
    return;
  }
  try {
    replay_->append(replay_frame_);
  } catch (const std::exception &error) {
    SDL_Log("Replay recording stopped: %s", error.what());
    replay_.reset();
  }
}
std::vector<std::string> TrainingScreen::take_sounds() {
  if (replay_) {
    std::vector<SoundCue> cues;
    for (const auto &sound : sounds_) {
      cues.push_back({SoundCue::Action::play, sound});
    }
    replay_->hear(cues);
  }
  return std::exchange(sounds_, {});
}
} // namespace phantomcrash
