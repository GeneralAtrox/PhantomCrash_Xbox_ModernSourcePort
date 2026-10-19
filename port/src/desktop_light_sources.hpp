#pragma once
#include "desktop_dynamic_lights.hpp"
#include "phantomcrash/data_roots.hpp"

#include <string>
#include <vector>

namespace phantomcrash {
// A light carried by a model's part: `mesh` names the part whose frame its
// position and direction are in (-1: the model's own frame).
struct AttachedLight {
  WorldLight light;
  int mesh{-1};
};

// Optional port-authored lighting, overlaid by mods/data/world/dynamic_lighting.json.
[[nodiscard]] std::vector<AttachedLight> light_sources(const DataRoots &roots, const char *group,
                                                       int stage);
// The base textures of the stage's painted light pools, which its lights
// replace while dynamic lighting is on ("light_pools").
[[nodiscard]] std::vector<std::string> light_pool_textures(const DataRoots &roots, int stage);
} // namespace phantomcrash
