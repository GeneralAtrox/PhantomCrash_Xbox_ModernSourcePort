#pragma once

// MechAssault's weapon effects as the converter writes them
// (data/world/mechassault_vfx.json, src/phantomcrash_research/mechassault):
// a node tree of meshes plus the few modifiers that move them. Times are in
// seconds here. The port draws them over its own weapon effects when the
// mod sets "enabled".

#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"

#include <map>
#include <string>
#include <vector>

namespace phantomcrash {

enum class VfxNodeKind { group, mesh, facing };

// How a node moves and fades, from the effect's modifiers.
struct VfxMotion {
  float kill{-1.0F}; // seconds; below 0 it lives as long as the effect
  Vec3 spin;         // radians per second about the node's own axes
  Vec3 scale_min{1.0F, 1.0F, 1.0F};
  Vec3 scale_max{1.0F, 1.0F, 1.0F};
  Vec3 scale_delta; // per second, from the start
  float alpha_rate{};
};

struct VfxNode {
  std::string name;
  VfxNodeKind kind{VfxNodeKind::group};
  int parent{-1};
  Vec3 position;
  Vec3 axis{1.0F, 0.0F, 0.0F};
  float angle{};
  Vec3 scale{1.0F, 1.0F, 1.0F};
  std::string model;
  VfxMotion motion;
};

struct VfxEffect {
  std::vector<VfxNode> nodes;
  float length{-1.0F}; // the last kill time; below 0 it has none
};

// The weapon kind's three effects (keys of VfxData::effects; empty: none).
struct VfxWeapon {
  std::string fire;
  std::string shot;
  std::string hit;
};

struct VfxData {
  bool enabled{};
  float unit_scale{0.35F};
  std::map<std::string, VfxWeapon> weapons; // by the HUD kind's name
  std::map<std::string, VfxEffect> effects;
};

[[nodiscard]] VfxData parse_vfx(const json::JsonValue &document);

} // namespace phantomcrash
