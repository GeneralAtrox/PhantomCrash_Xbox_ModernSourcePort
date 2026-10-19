#include "phantomcrash/world_sound.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {

SoundFalloff parse_sound_falloff(const json::JsonValue &value) {
  SoundFalloff falloff;
  const auto number = [&](const char *key, const float fallback) {
    return static_cast<float>(value.number_or(key, fallback));
  };
  falloff.distance_scale = number("distance_scale", falloff.distance_scale);
  falloff.min_distance = std::max(number("min_distance", falloff.min_distance), 1e-3F);
  falloff.rolloff = std::max(number("rolloff", falloff.rolloff), 0.0F);
  return falloff;
}

SoundMix place_sound(const SoundFalloff &falloff, const SoundListener &listener,
                     const Vec3 source) {
  const Vec3 offset = source - listener.eye;
  const float distance = length(offset) * falloff.distance_scale;
  SoundMix mix;
  if (distance > falloff.min_distance) {
    // DirectSound's inverse distance law.
    mix.gain = falloff.min_distance /
               (falloff.min_distance + falloff.rolloff * (distance - falloff.min_distance));
  }
  const Vec3 right = cross(listener.forward, listener.up);
  const float across = length(right);
  if (distance > 1e-4F && across > 1e-4F) {
    mix.pan = std::clamp(dot(offset, right) / (length(offset) * across), -1.0F, 1.0F);
  }
  return mix;
}

} // namespace phantomcrash
