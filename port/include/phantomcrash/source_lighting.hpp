#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"
#include <array>
#include <span>
#include <vector>

namespace phantomcrash {
struct SourceLightVolume {
  Vec3 position;
  Vec3 colour;
  Vec3 half_x;
  Vec3 half_z;
  float radius{};
};
struct SourceLightSample {
  Vec3 toward;
  Vec3 colour;
};

[[nodiscard]] std::vector<SourceLightVolume> parse_source_lights(const json::JsonValue &value);
[[nodiscard]] Vec3 source_light_emitter(const SourceLightVolume &light, Vec3 at);
[[nodiscard]] std::array<SourceLightSample, 2>
sample_source_lights(std::span<const SourceLightVolume> lights, Vec3 at);
} // namespace phantomcrash
