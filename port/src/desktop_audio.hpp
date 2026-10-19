#pragma once

#include "music_decoder.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/effect_mixer.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/world_sound.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace phantomcrash {

// Music and sound effects on SDL audio streams, which SDL mixes on one
// device. Music streams through FFmpeg and loops whole (the original's
// tracks have no loop points); effects are loaded once, at one rate, and
// mixed on one stream by an EffectMixer, each voice with its own gain and
// pan (the battle's placed sounds, world_sound.hpp). Names come from
// data/audio/music.json and sfx.json. Missing files are logged, not fatal.
class AudioMixer final {
public:
  AudioMixer(const DataRoots &roots, const json::JsonValue &music, const json::JsonValue &effects);
  ~AudioMixer();
  AudioMixer(const AudioMixer &) = delete;
  AudioMixer &operator=(const AudioMixer &) = delete;

  // Starts a track, or stops the music for an empty name; a name with a '/'
  // is an audio file, which loops unless `loop` is false (the radio's songs).
  // The same track carries on without a restart. `volume` scales the
  // track's own. A track that does not loop ends by itself; music() still
  // names it, and playing() is then false.
  void play_music(const std::string &track, float volume = 1.0F, bool loop = true);
  // A named effect, or an effect file when the name has a '/' (SE TEST).
  void play_sound(const std::string &name, SoundMix mix = {}, AudioBus bus = AudioBus::ui);
  // Plays, moves or stops a sound (a keyed loop by its key).
  void play_cue(const SoundCue &cue);
  // Loads an effect (a name or a file, as play_sound takes) ahead of its
  // first play.
  void preload(const std::string &name);
  // Stops every voice still playing the named effect.
  void stop_sound(const std::string &name);
  // Stops keyed and spatial sounds when their screen ends.
  void stop_keyed();
  [[nodiscard]] bool sound_playing(std::uint64_t key) const;
  // The game's BGM and SE volumes as gains; they apply to what is playing.
  void set_volumes(float music, float effects);
  void set_audio_options(const AudioOptions &options);
  // Keeps the music queue topped up, and notices a track's end; once per frame.
  void update();
  void set_paused(bool paused);
  // Freeze battle/music playback while keeping pause-menu effects audible.
  void set_gameplay_paused(bool paused);

  [[nodiscard]] const std::string &music() const noexcept {
    return track_;
  }
  [[nodiscard]] bool playing() const noexcept {
    return music_ != nullptr;
  }
  // Fills `samples` with the music being heard now (the queue ahead of it
  // skipped), as mono before any volume, and gives its sample rate. False
  // when nothing is heard: no music, or the music volume at 0.
  bool music_heard(std::vector<float> &samples, int &rate) const;

private:
  struct Track {
    std::string file;
    bool loop{true};
    float volume{1.0F};
  };
  struct Effect {
    std::shared_ptr<const EffectSamples> samples;
    float volume{1.0F};
    bool loaded{};
  };
  static constexpr int effect_rate = 44100;

  void stop_music();
  bool open_music(const Track &track);
  // desktop_audio_effects.cpp
  void open_effects();
  Effect *effect(const std::string &name);
  std::shared_ptr<const EffectSamples> source_sequence(const std::filesystem::path &path);
  // Keeps the music's samples as they are queued, for music_heard().
  void keep_heard(const std::uint8_t *bytes, std::size_t size);
  // SDL asks the effects stream for more: the mixer fills it.
  static void SDLCALL feed_effects(void *self, SDL_AudioStream *stream, int wanted, int total);

  const DataRoots &roots_;
  SDL_AudioDeviceID device_{};
  std::map<std::string, Track> tracks_;
  std::map<std::string, Effect> effects_;
  std::map<std::string, std::size_t> effect_limits_;
  std::map<std::string, std::shared_ptr<const EffectSamples>> sample_files_;
  std::map<std::string, std::pair<std::string, float>> effect_files_;
  std::string track_;
  SDL_AudioStream *music_{};
  MusicDecoder music_decoder_;
  bool music_loop_{};
  bool gameplay_paused_{};
  float music_track_volume_{1.0F};
  float music_volume_{1.0F}; // the screen's scale of the track's volume
  float music_gain_{1.0F};
  AudioOptions audio_options_;
  std::array<std::size_t, audio_bus_count> cue_counts_{};
  float effect_gain_{1.0F};
  int music_bytes_per_second_{};
  SDL_AudioSpec music_spec_{};
  std::vector<float> heard_;      // mono, a ring over the last few seconds queued
  std::uint64_t queued_frames_{}; // frames queued since the track started
  SDL_AudioStream *effects_stream_{};
  EffectMixer mixer_;
  std::vector<float> mixed_;
  std::vector<std::uint64_t> keys_; // the keyed sounds started
};

} // namespace phantomcrash
