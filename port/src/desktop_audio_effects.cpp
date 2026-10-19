// AudioMixer's effects: loaded once and converted to one rate, mixed by an
// EffectMixer on one stream that SDL asks for more as it plays.

#include "desktop_audio.hpp"

#include <algorithm>

namespace phantomcrash {

void AudioMixer::open_effects() {
  const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, effect_rate};
  effects_stream_ = SDL_CreateAudioStream(&spec, nullptr);
  if (effects_stream_ == nullptr ||
      !SDL_SetAudioStreamGetCallback(effects_stream_, &AudioMixer::feed_effects, this) ||
      !SDL_BindAudioStream(device_, effects_stream_)) {
    SDL_Log("Effects stream failed: %s", SDL_GetError());
    SDL_DestroyAudioStream(effects_stream_);
    effects_stream_ = nullptr;
  }
}

void SDLCALL AudioMixer::feed_effects(void *self, SDL_AudioStream *stream, const int wanted,
                                      int /*total*/) {
  auto &audio = *static_cast<AudioMixer *>(self);
  constexpr int frame_bytes = 2 * static_cast<int>(sizeof(float));
  const auto frames = static_cast<std::size_t>(std::max(wanted, 0) / frame_bytes);
  if (frames == 0) {
    return;
  }
  audio.mixed_.resize(frames * 2);
  // SDL holds the stream's lock while it asks: the mixer is safe here.
  audio.mixer_.mix(audio.mixed_.data(), frames);
  SDL_PutAudioStreamData(stream, audio.mixed_.data(), static_cast<int>(frames) * frame_bytes);
}

AudioMixer::Effect *AudioMixer::effect(const std::string &name) {
  auto &loaded = effects_[name];
  if (loaded.loaded) {
    return loaded.samples == nullptr ? nullptr : &loaded;
  }
  loaded.loaded = true;
  std::pair<std::string, float> source{name, 1.0F};
  if (name.find('/') == std::string::npos) {
    const auto file = effect_files_.find(name);
    if (file == effect_files_.end()) {
      SDL_Log("No sound effect named %s", name.c_str());
      return nullptr;
    }
    source = file->second;
  }
  loaded.volume = source.second;
  if (const auto cached = sample_files_.find(source.first); cached != sample_files_.end()) {
    loaded.samples = cached->second;
    return &loaded;
  }
  const auto path = roots_.find_asset(source.first);
  if (path && path->extension() == ".json") {
    try {
      loaded.samples = source_sequence(*path);
      sample_files_[source.first] = loaded.samples;
      return &loaded;
    } catch (const std::exception &error) {
      SDL_Log("Source sound %s could not be loaded: %s", source.first.c_str(), error.what());
      return nullptr;
    }
  }
  SDL_AudioSpec spec{};
  Uint8 *samples = nullptr;
  Uint32 length = 0;
  if (!path || !SDL_LoadWAV(path->string().c_str(), &spec, &samples, &length)) {
    SDL_Log("Sound effect %s could not be loaded", source.first.c_str());
    return nullptr;
  }
  const SDL_AudioSpec target{SDL_AUDIO_F32, std::min(spec.channels, 2), effect_rate};
  Uint8 *converted = nullptr;
  int size = 0;
  const bool ok =
      SDL_ConvertAudioSamples(&spec, samples, static_cast<int>(length), &target, &converted, &size);
  SDL_free(samples);
  if (!ok) {
    SDL_Log("Sound effect %s could not be converted: %s", source.first.c_str(), SDL_GetError());
    return nullptr;
  }
  auto effect = std::make_shared<EffectSamples>();
  if (const auto limit = effect_limits_.find(source.first); limit != effect_limits_.end()) {
    effect->voice_limit = limit->second;
  }
  effect->channels = target.channels;
  const auto *floats = reinterpret_cast<const float *>(converted);
  effect->samples.assign(floats, floats + size / static_cast<int>(sizeof(float)));
  SDL_free(converted);
  loaded.samples = std::move(effect);
  sample_files_[source.first] = loaded.samples;
  return &loaded;
}

void AudioMixer::set_audio_options(const AudioOptions &options) {
  if (audio_options_ == options) {
    return;
  }
  audio_options_ = options;
  if (music_) {
    SDL_SetAudioStreamGain(music_,
                           music_track_volume_ * music_gain_ * options.gain(AudioBus::music));
  }
  if (effects_stream_) {
    SDL_LockAudioStream(effects_stream_);
    mixer_.set_audio_options(options);
    SDL_UnlockAudioStream(effects_stream_);
  }
  SDL_Log("Audio mix: master=%d music=%d weapons=%d robots=%d environment=%d pickups=%d "
          "interface=%d dialogue=%d movies=%d",
          options.master, options.levels[0], options.levels[1], options.levels[2],
          options.levels[3], options.levels[4], options.levels[5], options.levels[6],
          options.levels[7]);
}

void AudioMixer::preload(const std::string &name) {
  static_cast<void>(effect(name));
}

void AudioMixer::play_sound(const std::string &name, const SoundMix mix, const AudioBus bus) {
  SoundCue cue{SoundCue::Action::play, name, mix};
  cue.bus = bus;
  play_cue(cue);
}

void AudioMixer::play_cue(const SoundCue &cue) {
  if (effects_stream_ == nullptr) {
    return;
  }
  Effect *sound = cue.action == SoundCue::Action::play ? effect(cue.name) : nullptr;
  if (cue.action == SoundCue::Action::play && sound == nullptr) {
    return;
  }
  SDL_LockAudioStream(effects_stream_);
  switch (cue.action) {
  case SoundCue::Action::play:
    mixer_.play(sound->samples, cue.name, sound->volume, cue.mix, cue.loop, cue.key, cue.position,
                cue.bus);
    ++cue_counts_[static_cast<std::size_t>(cue.bus)];
    if (cue.key != 0 && std::ranges::find(keys_, cue.key) == keys_.end()) {
      keys_.push_back(cue.key);
    }
    break;
  case SoundCue::Action::move:
    mixer_.set_mix(cue.key, cue.mix, cue.position);
    break;
  case SoundCue::Action::stop:
    mixer_.release_key(cue.key);
    std::erase(keys_, cue.key);
    break;
  case SoundCue::Action::listener:
    mixer_.set_listener(cue.falloff, cue.listener);
    break;
  case SoundCue::Action::stop_spatial:
    mixer_.stop_spatial();
    break;
  }
  SDL_UnlockAudioStream(effects_stream_);
}

void AudioMixer::stop_sound(const std::string &name) {
  if (effects_stream_ == nullptr) {
    return;
  }
  SDL_LockAudioStream(effects_stream_);
  mixer_.stop_name(name);
  SDL_UnlockAudioStream(effects_stream_);
}

bool AudioMixer::sound_playing(const std::uint64_t key) const {
  if (effects_stream_ == nullptr) {
    return false;
  }
  SDL_LockAudioStream(effects_stream_);
  const bool result = mixer_.playing(key);
  SDL_UnlockAudioStream(effects_stream_);
  return result;
}

void AudioMixer::stop_keyed() {
  if (effects_stream_ == nullptr) {
    return;
  }
  SDL_LockAudioStream(effects_stream_);
  for (const std::uint64_t key : std::exchange(keys_, {})) {
    mixer_.stop_key(key);
  }
  mixer_.stop_spatial(); // do not leave battle gunfire audible over the next screen
  SDL_UnlockAudioStream(effects_stream_);
}

} // namespace phantomcrash
