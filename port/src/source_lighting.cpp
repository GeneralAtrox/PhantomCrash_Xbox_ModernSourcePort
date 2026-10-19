#include "phantomcrash/source_lighting.hpp"
#include "phantomcrash/json_fields.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace phantomcrash {
std::vector<SourceLightVolume> parse_source_lights(const json::JsonValue &value) {
  std::vector<SourceLightVolume> out;
  const auto *entries = value.find("source_lights");
  if (!entries) {
    return out;
  }
  for (const auto &entry : entries->array()) {
    const auto vector = [&](const char *key) {
      const auto a = json::numbers<3>(json::required(entry, key), key);
      for (float v : a) {
        if (!std::isfinite(v)) {
          throw std::runtime_error("Non-finite source light vector");
        }
      }
      return Vec3{a[0], a[1], a[2]};
    };
    SourceLightVolume light{vector("position"), vector("colour"), vector("half_x"),
                            vector("half_z"), static_cast<float>(entry.number_or("radius", 0))};
    if (!std::isfinite(light.radius) || light.radius <= 0 || light.colour.x < 0 ||
        light.colour.y < 0 || light.colour.z < 0 ||
        std::abs(dot(light.half_x, light.half_z)) >
            1e-4F * std::max(1.0F, length(light.half_x) * length(light.half_z))) {
      throw std::runtime_error("Invalid source light volume");
    }
    out.push_back(light);
  }
  return out;
}

Vec3 source_light_emitter(const SourceLightVolume &light, const Vec3 at) {
  Vec3 nearest = light.position;
  const Vec3 delta = at - light.position;
  for (const Vec3 axis : {light.half_x, light.half_z}) {
    const float square = dot(axis, axis);
    if (square > 0) {
      nearest = nearest + axis * std::clamp(dot(delta, axis) / square, -1.0F, 1.0F);
    }
  }
  return nearest;
}

std::array<SourceLightSample, 2>
sample_source_lights(const std::span<const SourceLightVolume> lights, const Vec3 at) {
  std::array<SourceLightSample, 2> out{};
  std::array<float, 2> nearest{std::numeric_limits<float>::max(),
                               std::numeric_limits<float>::max()};
  for (const auto &light : lights) {
    const Vec3 toward = source_light_emitter(light, at) - at;
    const float square = dot(toward, toward);
    if (square >= light.radius * light.radius) {
      continue;
    }
    const float distance = std::sqrt(square);
    const SourceLightSample sample{distance > 0 ? toward * (1.0F / distance) : Vec3{},
                                   light.colour * (1.0F - square / (light.radius * light.radius))};
    if (distance < nearest[0]) {
      nearest[1] = nearest[0];
      out[1] = out[0];
      nearest[0] = distance;
      out[0] = sample;
    } else if (distance < nearest[1]) {
      nearest[1] = distance;
      out[1] = sample;
    }
  }
  return out;
}
} // namespace phantomcrash
