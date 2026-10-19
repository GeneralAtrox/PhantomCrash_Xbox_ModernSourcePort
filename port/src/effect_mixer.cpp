#include "phantomcrash/effect_mixer.hpp"
#include "phantomcrash/world_sound.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {

float pan_left(const float pan) noexcept {
  return std::clamp(1.0F - pan, 0.0F, 1.0F);
}

float pan_right(const float pan) noexcept {
  return std::clamp(1.0F + pan, 0.0F, 1.0F);
}

EffectMixer::EffectMixer(const std::size_t voices, int output_rate)
    : voices_(std::max<std::size_t>(voices, 1)), output_rate_(output_rate) {}

EffectMixer::Voice &EffectMixer::free_voice() {
  Voice *oldest = nullptr;
  for (Voice &voice : voices_) {
    if (voice.samples == nullptr) {
      return voice;
    }
    if (!voice.loop && (oldest == nullptr || voice.started < oldest->started)) {
      oldest = &voice;
    }
  }
  if (oldest != nullptr) {
    return *oldest;
  }
  return *std::ranges::min_element(voices_, {}, &Voice::started);
}

void EffectMixer::play(std::shared_ptr<const EffectSamples> samples, const std::string &name,
                       const float volume, const SoundMix mix, const bool loop,
                       const std::uint64_t key, const std::optional<Vec3> position,
                       const AudioBus bus) {
  if (samples == nullptr || (samples->frames() == 0 && !samples->sequence)) {
    return;
  }
  Voice *voice = nullptr;
  if (key != 0) {
    const auto found = std::ranges::find(voices_, key, &Voice::key);
    voice = found != voices_.end() ? &*found : nullptr;
  }
  if (voice == nullptr) {
    // 0x0de8d0 cycles the bank entry's fixed set of buffers. Retriggering
    // a busy slot restarts it (0x0de660), rather than layering another copy.
    std::size_t count = 0;
    Voice *oldest = nullptr;
    for (Voice &candidate : voices_) {
      if (candidate.samples == samples) {
        ++count;
        if (oldest == nullptr || candidate.started < oldest->started) {
          oldest = &candidate;
        }
      }
    }
    if (samples->voice_limit != 0 && count >= samples->voice_limit) {
      voice = oldest;
    }
  }
  if (voice == nullptr) {
    voice = &free_voice();
  }
  *voice = Voice{std::move(samples), name, volume, mix, loop, key, 0, ++count_, position, bus};
  if (voice->samples->sequence) {
    voice->sequence.emplace(voice->samples->sequence, output_rate_, loop);
  }
}

void EffectMixer::set_mix(const std::uint64_t key, const SoundMix mix,
                          const std::optional<Vec3> position) {
  for (Voice &voice : voices_) {
    if (key != 0 && voice.key == key) {
      voice.mix = mix;
      voice.position = position;
    }
  }
}

void EffectMixer::set_listener(const SoundFalloff &falloff, const SoundListener &listener) {
  for (Voice &voice : voices_) {
    if (voice.samples && voice.position) {
      voice.mix = place_sound(falloff, listener, *voice.position);
    }
  }
}

void EffectMixer::stop_spatial() {
  for (Voice &voice : voices_) {
    if (voice.position) {
      voice = Voice{};
    }
  }
}

void EffectMixer::stop_key(const std::uint64_t key) {
  for (Voice &voice : voices_) {
    if (key != 0 && voice.key == key) {
      voice = Voice{};
    }
  }
}

void EffectMixer::release_key(const std::uint64_t key) {
  for (Voice &voice : voices_) {
    if (key != 0 && voice.key == key) {
      if (voice.sequence) {
        voice.sequence->release();
        voice.key = 0;
        voice.loop = false;
      } else {
        voice = Voice{};
      }
    }
  }
}

void EffectMixer::stop_name(const std::string &name) {
  for (Voice &voice : voices_) {
    if (voice.samples != nullptr && voice.name == name) {
      voice = Voice{};
    }
  }
}

void EffectMixer::stop_all() {
  std::ranges::fill(voices_, Voice{});
}

bool EffectMixer::playing(const std::uint64_t key) const {
  return std::ranges::any_of(voices_, [&](const Voice &voice) {
    return key != 0 && voice.key == key && voice.samples != nullptr;
  });
}

std::size_t EffectMixer::active() const {
  return static_cast<std::size_t>(
      std::ranges::count_if(voices_, [](const Voice &voice) { return voice.samples != nullptr; }));
}

void EffectMixer::mix(float *out, const std::size_t frames) {
  std::fill(out, out + frames * 2, 0.0F);
  for (Voice &voice : voices_) {
    if (voice.samples == nullptr || (gameplay_paused_ && voice.bus != AudioBus::ui)) {
      continue;
    }
    const EffectSamples &sound = *voice.samples;
    const auto channels = static_cast<std::size_t>(sound.channels);
    const std::size_t length = sound.frames();
    const float level = voice.volume * voice.mix.gain * gain_ * audio_.gain(voice.bus);
    const float left = level * pan_left(voice.mix.pan);
    const float right = level * pan_right(voice.mix.pan);
    if (voice.sequence) {
      for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto sample = voice.sequence->sample();
        out[frame * 2] += sample[0] * left;
        out[frame * 2 + 1] += sample[1] * right;
        if (voice.sequence->finished()) {
          voice = Voice{};
          break;
        }
      }
      continue;
    }
    for (std::size_t frame = 0; frame < frames; ++frame) {
      if (voice.at >= length) {
        if (!voice.loop) {
          voice = Voice{};
          break;
        }
        voice.at = 0;
      }
      const float *source = sound.samples.data() + voice.at * channels;
      const float first = source[0];
      const float second = channels > 1 ? source[1] : first;
      out[frame * 2] += first * left;
      out[frame * 2 + 1] += second * right;
      ++voice.at;
    }
    if (voice.samples && !voice.loop && voice.at >= length) {
      voice = Voice{};
    }
  }
  for (std::size_t index = 0; index < frames * 2; ++index) {
    out[index] = std::clamp(out[index], -1.0F, 1.0F);
  }
}

} // namespace phantomcrash
