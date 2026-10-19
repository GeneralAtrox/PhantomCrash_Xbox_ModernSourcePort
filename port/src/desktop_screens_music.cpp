// The runner's music: the track a screen names, the volumes, the radio's
// songs and the spectrum the screens show.

#include "desktop_screens.hpp"

#include "phantomcrash/sound_config.hpp"

#include <SDL3/SDL_log.h>

#include <utility>

namespace phantomcrash {

std::string ScreenRunner::music_file(const std::string &track) const {
  if (track == "keep") {
    return audio_.music() == "keep" ? std::string{} : music_file(audio_.music());
  }
  if (track.find('/') != std::string::npos) {
    return track;
  }
  const auto file = track_files_.find(track);
  return file == track_files_.end() ? std::string{} : file->second;
}

void ScreenRunner::set_audio_options(const AudioOptions &options) {
  audio_options_ = options;
  audio_.set_audio_options(options);
  if (screen_) {
    screen_->set_movie_gain(options.gain(AudioBus::movies));
  }
}

void ScreenRunner::apply_volumes() {
  audio_.set_volumes(volume_gain(options_.bgm_volume, volume_millibels_),
                     volume_gain(options_.se_volume, volume_millibels_));
}

void ScreenRunner::play_song(const std::string &file) {
  audio_.play_music({});
  audio_.play_music(file, radio_->volume(), false);
}

void ScreenRunner::tell_music(const bool always) {
  std::string file = audio_.playing() ? music_file(audio_.music()) : std::string{};
  if (always || file != told_music_) {
    told_music_ = std::move(file);
    screen_->set_music(told_music_);
    if (overlay_) {
      overlay_->set_music(told_music_);
    }
  }
}

void ScreenRunner::play_music() {
  if (screen_->uses_music_program() && play_music_program()) {
    return;
  }
  if (const std::string music = screen_->music(); music != "keep") {
    audio_.play_music(music, screen_->music_volume());
  }
}

void ScreenRunner::reset_music_program() {
  program_file_.clear();
  program_track_ = -1;
  std::vector<int> tracks;
  const MusicLibrary &library = quest_.music;
  if (screen_->uses_music_program() && library.program >= 0 &&
      library.program < music_program_count) {
    for (const int track : library.programs[static_cast<std::size_t>(library.program)]) {
      if (!library.owns(track) || track < 0 || track >= static_cast<int>(credits_.size())) {
        SDL_Log("Battle music: skipping unavailable credit=%d", track);
        continue;
      }
      const std::string &file = credits_[static_cast<std::size_t>(track)].file;
      if (file.empty() || !roots_.find_asset(file)) {
        SDL_Log("Battle music: missing credit=%d file=%s", track, file.c_str());
        continue;
      }
      tracks.push_back(track);
    }
    SDL_Log("Battle music: program=%d mode=%s playable=%zu fallback=%s", library.program + 1,
            library.random ? "random" : "sequential", tracks.size(),
            tracks.empty() ? screen_->music().c_str() : "none");
  }
  music_program_.reset(std::move(tracks), library.random);
}

bool ScreenRunner::play_music_program() {
  if (music_program_.empty()) {
    return false;
  }
  // 0x09e24c starts the next song only after a stretch of silence.
  if (!audio_.playing() && music_silence_ + 1e-9 < screen_->music_program_gap()) {
    return true;
  }
  // A scene cue may interrupt the song. Retain its place in the program;
  // restart that song after the cue, rather than skipping to the next one.
  if (program_file_.empty() || (audio_.music() == program_file_ && !audio_.playing())) {
    program_track_ = music_program_.next().value();
    program_file_ = credits_[static_cast<std::size_t>(program_track_)].file;
  } else if (audio_.music() == program_file_ && audio_.playing()) {
    return true;
  }
  while (!music_program_.empty()) {
    // An explicit restart also handles a one-song program and duplicate entries.
    audio_.play_music({});
    audio_.play_music(program_file_, screen_->music_volume(), false);
    if (audio_.playing()) {
      SDL_Log("Battle music: playing credit=%d file=%s", program_track_, program_file_.c_str());
      return true;
    }
    SDL_Log("Battle music: failed credit=%d; skipping", program_track_);
    music_program_.reject(program_track_);
    const auto next = music_program_.next();
    if (!next) {
      program_file_.clear();
      return false;
    }
    program_track_ = *next;
    program_file_ = credits_[static_cast<std::size_t>(program_track_)].file;
  }
  return false;
}

void ScreenRunner::hear_music(const double tick_seconds) {
  int rate = 0;
  if (audio_.music_heard(heard_samples_, rate)) {
    spectrum_.hear(heard_samples_, rate, tick_seconds);
  } else {
    spectrum_.silence();
  }
  (overlay_ ? overlay_ : screen_)->hear_music(spectrum_.levels());
}

} // namespace phantomcrash
