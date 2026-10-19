#include "desktop_audio.hpp"
#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <cstring>
#include <exception>

namespace phantomcrash {
namespace {

// Frames of queued music kept for music_heard(): about 3 s, well over the
// queue ahead plus a chunk.
constexpr std::size_t heard_frames = std::size_t{1} << 17U;

std::uint32_t read_u32(const std::uint8_t *bytes) {
  return bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) |
         (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::uint16_t read_u16(const std::uint8_t *bytes) {
  return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
}

} // namespace

AudioMixer::AudioMixer(const DataRoots &roots, const json::JsonValue &music,
                       const json::JsonValue &effects)
    : roots_(roots), heard_(heard_frames) {
  if (const auto *tracks = music.find("tracks")) {
    for (const auto &[name, value] : tracks->object()) {
      tracks_[name] = {value.string_or("file", ""), value.boolean_or("loop", true),
                       static_cast<float>(value.number_or("volume", 1.0))};
    }
  }
  if (const auto *list = effects.find("effects")) {
    for (const auto &[name, value] : list->object()) {
      effect_files_[name] = {value.string_or("file", ""),
                             static_cast<float>(value.number_or("volume", 1.0))};
    }
  }
  if (const auto file = roots.find_asset("tables/sound_effects.json")) {
    const auto bank = json::read_file(*file);
    std::size_t voices = 0;
    for (const auto &[name, value] : json::required(bank, "effects").object()) {
      const auto count =
          static_cast<std::size_t>(std::clamp(value.number_or("voices", 1), 1.0, 255.0));
      effect_limits_[name] = count;
      voices += count;
    }
    if (voices > 0) {
      mixer_ = EffectMixer(voices);
    }
  }
  device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
  if (device_ == 0) {
    SDL_Log("No audio device for music and effects: %s", SDL_GetError());
    return;
  }
  open_effects();
}

AudioMixer::~AudioMixer() {
  for (std::size_t i = 0; i < audio_bus_count; ++i) {
    if (i == static_cast<std::size_t>(AudioBus::movies)) {
      continue;
    }
    SDL_Log("Audio coverage: bus=%s submitted=%zu", std::string(audio_bus_names[i]).c_str(),
            cue_counts_[i]);
  }
  stop_music();
  SDL_DestroyAudioStream(effects_stream_);
  if (device_ != 0) {
    SDL_CloseAudioDevice(device_);
  }
}

void AudioMixer::stop_music() {
  SDL_DestroyAudioStream(music_);
  music_ = nullptr;
  queued_frames_ = 0;
  music_decoder_.close();
}

bool AudioMixer::open_music(const Track &track) {
  const auto path = roots_.find_asset(track.file);
  if (!path) {
    SDL_Log("Music %s not found", track.file.c_str());
    return false;
  }
  try {
    music_decoder_.open(*path);
  } catch (const std::exception &error) {
    SDL_Log("Music %s: %s", track.file.c_str(), error.what());
    return false;
  }
  const SDL_AudioSpec spec{SDL_AUDIO_F32, MusicDecoder::channels, MusicDecoder::rate};
  music_ = SDL_CreateAudioStream(&spec, nullptr);
  if (music_ == nullptr || (!gameplay_paused_ && !SDL_BindAudioStream(device_, music_))) {
    SDL_Log("Music stream failed: %s", SDL_GetError());
    return false;
  }
  music_track_volume_ = track.volume * music_volume_;
  SDL_SetAudioStreamGain(music_,
                         music_track_volume_ * music_gain_ * audio_options_.gain(AudioBus::music));
  music_loop_ = track.loop;
  music_bytes_per_second_ = SDL_AUDIO_FRAMESIZE(spec) * spec.freq;
  music_spec_ = spec;
  return true;
}

void AudioMixer::play_music(const std::string &track, const float volume, const bool loop) {
  if (track == track_) {
    return;
  }
  track_ = track;
  music_volume_ = volume;
  stop_music();
  if (track.empty() || device_ == 0) {
    return;
  }
  Track file{track, loop, 1.0F};
  if (track.find('/') == std::string::npos) {
    const auto found = tracks_.find(track);
    if (found == tracks_.end()) {
      SDL_Log("No music track named %s", track.c_str());
      return;
    }
    file = found->second;
  }
  if (!open_music(file)) {
    stop_music();
    return;
  }
  ++cue_counts_[static_cast<std::size_t>(AudioBus::music)];
  SDL_Log("Music playback: track=%s loop=%d rate=%d channels=%d", track.c_str(), music_loop_,
          music_spec_.freq, music_spec_.channels);
  update();
}

void AudioMixer::update() {
  if (music_ == nullptr) {
    return;
  }
  // Half a second queued ahead is plenty and keeps a track change prompt.
  try {
    bool restarted = false;
    while (SDL_GetAudioStreamQueued(music_) < music_bytes_per_second_ / 2) {
      const auto samples = music_decoder_.read();
      if (samples.empty()) {
        if (!music_loop_) {
          SDL_FlushAudioStream(music_);
          if (SDL_GetAudioStreamQueued(music_) == 0) {
            stop_music();
          }
          return;
        }
        if (restarted) {
          SDL_Log("Music has no playable samples; stopping it");
          stop_music();
          return;
        }
        music_decoder_.rewind();
        restarted = true;
        continue;
      }
      restarted = false;
      const auto bytes = samples.size() * sizeof(float);
      if (!SDL_PutAudioStreamData(music_, samples.data(), static_cast<int>(bytes))) {
        throw std::runtime_error(SDL_GetError());
      }
      keep_heard(reinterpret_cast<const std::uint8_t *>(samples.data()), bytes);
    }
  } catch (const std::exception &error) {
    SDL_Log("Music playback failed: %s", error.what());
    stop_music();
  }
}

void AudioMixer::keep_heard(const std::uint8_t *bytes, const std::size_t size) {
  const int channels = std::max(music_spec_.channels, 1);
  const auto frame = static_cast<std::size_t>(SDL_AUDIO_FRAMESIZE(music_spec_));
  if (frame == 0) {
    return;
  }
  const std::size_t sample_bytes = frame / static_cast<std::size_t>(channels);
  for (std::size_t at = 0; at + frame <= size; at += frame) {
    float sum = 0.0F;
    for (int channel = 0; channel < channels; ++channel) {
      const std::uint8_t *sample = bytes + at + static_cast<std::size_t>(channel) * sample_bytes;
      if (music_spec_.format == SDL_AUDIO_S16LE) {
        sum += static_cast<float>(static_cast<std::int16_t>(read_u16(sample))) / 32768.0F;
      } else if (music_spec_.format == SDL_AUDIO_U8) {
        sum += (static_cast<float>(*sample) - 128.0F) / 128.0F;
      } else {
        float value = 0.0F;
        std::memcpy(&value, sample, sizeof(value));
        sum += value;
      }
    }
    heard_[queued_frames_++ & (heard_frames - 1)] = sum / static_cast<float>(channels);
  }
}

bool AudioMixer::music_heard(std::vector<float> &samples, int &rate) const {
  if (music_ == nullptr ||
      music_track_volume_ * music_gain_ * audio_options_.gain(AudioBus::music) <= 0.0F) {
    return false;
  }
  // What is heard now is the last frame queued less those still waiting.
  const auto waiting = static_cast<std::uint64_t>(std::max(SDL_GetAudioStreamQueued(music_), 0)) /
                       static_cast<std::uint64_t>(SDL_AUDIO_FRAMESIZE(music_spec_));
  const std::uint64_t now = queued_frames_ - std::min(waiting, queued_frames_);
  const std::uint64_t count = std::min<std::uint64_t>(samples.size(), heard_frames);
  for (std::uint64_t index = 0; index < count; ++index) {
    const std::uint64_t at = now - count + index; // wraps below 0 before the track's start
    samples[index] = at < now ? heard_[at & (heard_frames - 1)] : 0.0F;
  }
  rate = music_spec_.freq;
  return true;
}

void AudioMixer::set_volumes(const float music, const float effects) {
  if (music != music_gain_) {
    music_gain_ = music;
    if (music_ != nullptr) {
      SDL_SetAudioStreamGain(music_, music_track_volume_ * music_gain_ *
                                         audio_options_.gain(AudioBus::music));
    }
  }
  if (effects != effect_gain_) {
    effect_gain_ = effects;
    if (effects_stream_ != nullptr) {
      SDL_SetAudioStreamGain(effects_stream_, effect_gain_);
    }
  }
}

void AudioMixer::set_gameplay_paused(const bool paused) {
  if (gameplay_paused_ == paused) {
    return;
  }
  gameplay_paused_ = paused;
  if (effects_stream_) {
    SDL_LockAudioStream(effects_stream_);
    mixer_.set_gameplay_paused(paused);
    if (paused) {
      SDL_ClearAudioStream(effects_stream_);
    }
    SDL_UnlockAudioStream(effects_stream_);
  }
  if (music_) {
    if (paused) {
      SDL_UnbindAudioStream(music_);
    } else if (!SDL_BindAudioStream(device_, music_)) {
      SDL_Log("Could not resume battle music: %s", SDL_GetError());
    }
  }
}

void AudioMixer::set_paused(const bool paused) {
  if (device_ == 0) {
    return;
  }
  if (paused) {
    SDL_PauseAudioDevice(device_);
  } else {
    SDL_ResumeAudioDevice(device_);
  }
}

} // namespace phantomcrash
