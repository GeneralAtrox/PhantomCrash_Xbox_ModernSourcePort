#include "desktop_light_sources.hpp"
#include "phantomcrash/json_fields.hpp"
#include <cmath>
#include <stdexcept>

namespace phantomcrash {
std::vector<AttachedLight> light_sources(const DataRoots &roots, const char *group, int stage) {
  std::vector<AttachedLight> out;
  const auto data = roots.document("world/dynamic_lighting.json");
  const auto *groups = data.find(group);
  const auto *entries = groups ? groups->find(std::to_string(stage)) : nullptr;
  if (!entries) {
    return out;
  }
  for (const auto &entry : entries->array()) {
    const auto vector = [&](const char *key) {
      const auto a = json::numbers<3>(json::required(entry, key), key);
      for (float x : a) {
        if (!std::isfinite(x)) {
          throw std::runtime_error("Non-finite light vector");
        }
      }
      return Vec3{a[0], a[1], a[2]};
    };
    WorldLight light;
    light.position = vector("position");
    light.direction = normalize(vector("direction"));
    light.colour = vector("colour");
    light.intensity = static_cast<float>(entry.number_or("intensity", 2));
    light.range = static_cast<float>(entry.number_or("range", 200));
    light.inner_degrees = static_cast<float>(entry.number_or("inner_degrees", 20));
    light.outer_degrees = static_cast<float>(entry.number_or("outer_degrees", 30));
    light.near_plane = static_cast<float>(entry.number_or("near", light.near_plane));
    light.air = static_cast<float>(entry.number_or("air", light.air));
    light.air_fade = static_cast<float>(entry.number_or("air_fade", light.air_fade));
    if (entry.find("bar")) {
      light.bar = vector("bar");
    }
    if (!std::isfinite(light.intensity) || light.intensity < 0 || !std::isfinite(light.range) ||
        light.range <= 0.5F || !(light.near_plane >= 0.05F && light.near_plane < light.range) ||
        !(light.inner_degrees >= 0 && light.inner_degrees < light.outer_degrees &&
          light.outer_degrees < 89) ||
        !(light.air >= 0) || !(light.air_fade >= 0) || length(light.direction) < 0.9F ||
        light.colour.x < 0 || light.colour.y < 0 || light.colour.z < 0) {
      throw std::runtime_error("Invalid dynamic spotlight settings");
    }
    out.push_back({light, static_cast<int>(entry.number_or("mesh", -1))});
  }
  return out;
}

std::vector<std::string> light_pool_textures(const DataRoots &roots, const int stage) {
  std::vector<std::string> out;
  const auto data = roots.document("world/dynamic_lighting.json");
  const auto *pools = data.find("light_pools");
  if (const auto *entries = pools ? pools->find(std::to_string(stage)) : nullptr) {
    for (const auto &entry : entries->array()) {
      out.push_back(entry.string());
    }
  }
  return out;
}
} // namespace phantomcrash
