// The training battle's sounds: the robots' own, their weapons', the
// doors' and the pickups', each placed where it happens and heard from the
// camera (world_sound.hpp). The split screen hears them whole, as the
// original does (0x09df75).

#include "desktop_training.hpp"

#include <SDL3/SDL.h>

namespace phantomcrash {

namespace {

constexpr const char *battle_sounds_document = "audio/battle_sounds.json";

} // namespace

bool TrainingScreen::battle_music_ended() const {
  // A match's exit walk keeps the battle track playing; RESULT's own music
  // takes over once the battle is done.
  const bool exiting = !match_results_ && scene_ && scene_->scene().state() == scene_state::exit;
  return !finale_ && (done_ || exiting);
}

std::string TrainingScreen::music() const {
  if (battle_music_ended()) {
    return {};
  }
  if (finale_) {
    return finale_music();
  }
  if (!cue_music_.empty()) {
    return cue_music_;
  }
  return music_start_ == MusicStart::open ? script_.music : std::string{};
}

bool TrainingScreen::uses_music_program() const {
  // Otherwise the runner would start the next playlist track after the exit stops it.
  return free_play_ && script_.music_program && cue_music_.empty() && !finale_ &&
         music_start_ == MusicStart::open && !battle_music_ended();
}

void TrainingScreen::tick_music_start(const double seconds) {
  const auto start_gap = [&] {
    music_start_ = MusicStart::gap;
    music_gap_left_ = script_.battle.scenes.music_gap_seconds;
  };
  switch (music_start_) {
  case MusicStart::entrance: {
    // The entrance cues the jingle 6 s before it ends (0x08975e); without one the
    // gap starts as the entrance does end.
    const bool entering = scene_ && scene_->scene().state() != scene_state::none;
    entrance_seen_ = entrance_seen_ || entering;
    if (!cue_music_.empty()) {
      music_start_ = MusicStart::play_in;
    } else if (entrance_seen_ && !entering) {
      start_gap();
    }
    break;
  }
  case MusicStart::play_in:
    if (cue_music_.empty()) {
      start_gap(); // the jingle is over (or would not play)
    }
    break;
  case MusicStart::gap:
    music_gap_left_ -= seconds;
    if (music_gap_left_ <= 1e-9) {
      music_start_ = MusicStart::open;
    }
    break;
  case MusicStart::open:
    break;
  }
}

void TrainingScreen::queue_dialogue(std::vector<std::string> sounds) {
  for (auto &sound : sounds) {
    SoundCue cue{SoundCue::Action::play, std::move(sound)};
    cue.bus = AudioBus::dialogue;
    unplaced_.push_back(std::move(cue));
  }
}

void TrainingScreen::load_sounds(const DataRoots &roots, BattleSounds::PlaybackQuery playing) {
  try {
    battle_sounds_ = std::make_unique<BattleSounds>(
        parse_battle_sounds(roots.document(battle_sounds_document)), std::move(playing));
  } catch (const std::exception &error) {
    SDL_Log("%s: %s", battle_sounds_document, error.what());
  }
}

void TrainingScreen::gather_sounds(const float seconds) {
  const auto &player = battle_->robots().at(static_cast<std::size_t>(player_index_));
  bool acquired = false;
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    const auto *weapon = player.weapons[slot];
    // Only a lock sight's countdown completes a lock (0x051045).
    const int target = player.alive && weapon && lock_sights(*weapon) ? player.locks[slot] : -1;
    acquired = acquired || (target >= 0 && target != heard_locks_[slot]);
    heard_locks_[slot] = target;
  }
  if (acquired && !paused_) {
    // Original lock completion: 0x05137d / 0x052153, bank 3 entry 0x0b.
    unplaced_.push_back({SoundCue::Action::play, "sound/sfx/battle_player/11.wav"});
  }
  // 0x093637: a living player's robot hears its prize change, once a tick.
  const std::int64_t prize = match_prize();
  if (prize != heard_prize_ && player.alive && !paused_ && !script_.battle.prize_sound.empty()) {
    unplaced_.push_back({SoundCue::Action::play, script_.battle.prize_sound});
  }
  heard_prize_ = prize;
  std::vector<WorldSound> sounds;
  if (StageDoors *doors = battle_->doors()) {
    sounds = doors->take_sounds();
  }
  gather_supply_sounds(*battle_, script_.battle.supply.look, sounds);
  if (StageObjects *objects = battle_->objects()) {
    for (auto &[name, at] : objects->take_sounds()) {
      sounds.push_back({std::move(name), at});
    }
  }
  for (WorldSound &sound : sounds) {
    SoundCue cue{SoundCue::Action::play, std::move(sound.name)};
    cue.bus = sound.bus;
    if (sound.at) {
      placed_.push_back({std::move(cue), *sound.at});
    } else {
      unplaced_.push_back(std::move(cue));
    }
  }
  if (battle_sounds_) {
    battle_sounds_->observe(*battle_, seconds);
    std::ranges::move(battle_sounds_->take(), std::back_inserter(placed_));
  }
}

std::vector<std::string> TrainingScreen::sound_preloads() const {
  std::vector<std::string> files;
  files.push_back("window");
  files.push_back("sound/sfx/battle_player/11.wav");
  if (battle_sounds_) {
    auto battle_files = sound_files(battle_sounds_->table());
    files.insert(files.end(), battle_files.begin(), battle_files.end());
  }
  files.push_back(script_.battle.settings.robot_wrecks.break_sound);
  if (!script_.battle.prize_sound.empty()) {
    files.push_back(script_.battle.prize_sound);
  }
  const SceneTuning &scenes = script_.battle.scenes;
  for (const std::string *name :
       {&scenes.doors.move_sound, &scenes.doors.open_sound, &scenes.ready_sound, &scenes.go_sound,
        &script_.battle.supply.look.take_sound}) {
    files.push_back(*name);
  }
  for (const SupplyKind &kind : script_.battle.supply.kinds) {
    files.insert(files.end(), kind.look.take_sounds.begin(), kind.look.take_sounds.end());
  }
  if (!script_.battle.supply.special.arrive_sound.empty()) {
    files.push_back(script_.battle.supply.special.arrive_sound);
  }
  return files;
}

std::vector<SoundCue> TrainingScreen::take_sound_cues() {
  const SoundFalloff falloff = battle_sounds_ ? battle_sounds_->table().falloff : SoundFalloff{};
  SoundListener listener{camera_.eye()};
  const Vec3 view = camera_.target() - camera_.eye();
  if (length(view) > 1e-4F) {
    listener.forward = normalize(view);
  }
  auto cues = place_cues(falloff, screens_ > 1 ? nullptr : &listener, std::exchange(placed_, {}));
  std::ranges::move(std::exchange(unplaced_, {}), std::back_inserter(cues));
  if (replay_) {
    replay_->hear(cues);
  }
  return cues;
}

} // namespace phantomcrash
