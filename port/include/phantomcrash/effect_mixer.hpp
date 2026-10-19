#pragma once
#include "phantomcrash/audio_options.hpp"
#include "phantomcrash/source_sound.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "phantomcrash/math3d.hpp"

namespace phantomcrash {

struct SoundFalloff;
struct SoundListener;

// How loud a sound is heard and where: `gain` 0..1 from its distance, and
// `pan` from -1 (left) through 0 (both sides whole) to 1 (right).
struct SoundMix {
  float gain{1.0F};
  float pan{0.0F};
};

// A loaded effect at the mixer's rate: interleaved frames of `channels`.
struct EffectSamples {
  std::vector<float> samples;
  int channels{1};
  std::size_t voice_limit{}; // SE BIN byte +2; zero leaves custom sounds unrestricted
  std::shared_ptr<const SourceSound> sequence;
  [[nodiscard]] std::size_t frames() const noexcept {
    return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0;
  }
};

// The effects' voices, mixed into stereo. A voice plays one effect once,
// or loops it until stopped; a keyed voice (key not 0) can be moved or
// stopped by its key. When every voice is busy the oldest one that does
// not loop is taken. Not thread-safe: the caller locks around it.
class EffectMixer final {
public:
  explicit EffectMixer(std::size_t voices = 24, int output_rate = 44100);

  // Starts `samples` (kept alive while it plays) at `volume` times `mix`.
  // A key already playing restarts.
  void play(std::shared_ptr<const EffectSamples> samples, const std::string &name, float volume,
            SoundMix mix, bool loop = false, std::uint64_t key = 0,
            std::optional<Vec3> position = {}, AudioBus bus = AudioBus::ui);
  void set_mix(std::uint64_t key, SoundMix mix, std::optional<Vec3> position = {});
  void set_listener(const SoundFalloff &falloff, const SoundListener &listener);
  void stop_spatial();
  void stop_key(std::uint64_t key);
  // Source sample sequences key off their live envelopes. Their detached
  // release tails can coexist with a new keyed trigger; PCM effects stop.
  void release_key(std::uint64_t key);
  void stop_name(const std::string &name);
  void stop_all();
  void set_gameplay_paused(bool paused) noexcept {
    gameplay_paused_ = paused;
  }
  // The SE volume, over every voice.
  void set_gain(float gain) noexcept {
    gain_ = gain;
  }
  void set_audio_options(const AudioOptions &options) noexcept {
    audio_ = options;
  }
  [[nodiscard]] bool playing(std::uint64_t key) const;
  [[nodiscard]] std::size_t active() const;
  // Writes `frames` stereo frames (left, right) into `out`.
  void mix(float *out, std::size_t frames);

private:
  struct Voice {
    std::shared_ptr<const EffectSamples> samples;
    std::string name;
    float volume{1.0F};
    SoundMix mix;
    bool loop{};
    std::uint64_t key{};
    std::size_t at{};
    std::uint64_t started{};
    std::optional<Vec3> position;
    AudioBus bus{AudioBus::ui};
    std::optional<SourceSoundPlayback> sequence;
  };

  Voice &free_voice();

  std::vector<Voice> voices_;
  float gain_{1.0F};
  bool gameplay_paused_{};
  AudioOptions audio_;
  std::uint64_t count_{};
  int output_rate_;
};

// Left and right gains for `pan`: the far side falls away, the near one
// stays whole.
[[nodiscard]] float pan_left(float pan) noexcept;
[[nodiscard]] float pan_right(float pan) noexcept;

} // namespace phantomcrash
