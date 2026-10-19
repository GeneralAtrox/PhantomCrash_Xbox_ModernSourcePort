#pragma once

#include "phantomcrash/effect_mixer.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace phantomcrash {

// The battle's sounds are DirectSound 3D voices: 0x0ddc50 and 0x0de390
// place a voice and the listener at half the world's distances (0.5,
// 0x17accc), every voice with a minimum distance of 1 (0x0ddd4f) and the
// listener with a rolloff factor of 0.1 and no Doppler (0x0de616). So a
// sound is whole within 2 world units, half as loud 22 away. The port pans
// by the side the sound is on, seen from the camera.
struct SoundFalloff {
  float distance_scale{0.5F};
  float min_distance{1.0F};
  float rolloff{0.1F};
};

// data/audio/battle_sounds.json "falloff"; missing values keep the original's.
[[nodiscard]] SoundFalloff parse_sound_falloff(const json::JsonValue &value);

// The camera hearing the world (0x0804eb: its eye and view).
struct SoundListener {
  Vec3 eye;
  Vec3 forward{0.0F, 0.0F, -1.0F};
  Vec3 up{0.0F, 1.0F, 0.0F};
};

[[nodiscard]] SoundMix place_sound(const SoundFalloff &falloff, const SoundListener &listener,
                                   Vec3 source);

// A sound for the runner to play: once (key 0), or a keyed loop that is
// started, moved and stopped by its key.
struct SoundCue {
  enum class Action { play, move, stop, listener, stop_spatial };
  Action action{Action::play};
  std::string name;
  SoundMix mix;
  bool loop{};
  std::uint64_t key{};
  std::optional<Vec3> position;
  SoundListener listener;
  SoundFalloff falloff;
  AudioBus bus{AudioBus::ui};
};

// A sound in the world, placed when heard; without `at` it is heard
// whole (the split screen's, 0x09df75, and the menus').
struct WorldSound {
  std::string name;
  std::optional<Vec3> at;
  AudioBus bus{AudioBus::environment};
};

} // namespace phantomcrash
