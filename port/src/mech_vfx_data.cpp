#include "mech_vfx_data.hpp"

#include <algorithm>

namespace phantomcrash {
namespace {

constexpr float seconds_per_ms = 0.001F;

Vec3 vec3_of(const json::JsonValue &value, const Vec3 fallback) {
  if (!value.is_array() || value.array().size() < 3) {
    return fallback;
  }
  const auto &a = value.array();
  return {static_cast<float>(a[0].number()), static_cast<float>(a[1].number()),
          static_cast<float>(a[2].number())};
}

Vec3 vec3_or(const json::JsonValue &parent, const char *key, const Vec3 fallback) {
  const json::JsonValue *value = parent.find(key);
  return value != nullptr ? vec3_of(*value, fallback) : fallback;
}

VfxNodeKind kind_of(const std::string &name) {
  if (name == "3dobject") {
    return VfxNodeKind::mesh;
  }
  return name == "facing" ? VfxNodeKind::facing : VfxNodeKind::group;
}

VfxNode parse_node(const json::JsonValue &value) {
  VfxNode node;
  node.name = value.string_or("name", "");
  node.kind = kind_of(value.string_or("kind", "node"));
  node.parent = static_cast<int>(value.number_or("parent", -1));
  node.position = vec3_or(value, "position", {});
  node.axis = vec3_or(value, "axis", node.axis);
  node.angle = static_cast<float>(value.number_or("angle", 0.0));
  node.scale = vec3_or(value, "scale", node.scale);
  node.model = value.string_or("model", "");
  return node;
}

void apply(VfxEffect &effect, const json::JsonValue &modifier) {
  const std::string name = modifier.string_or("SubObjectName", "");
  const std::string type = modifier.string_or("type", "");
  for (VfxNode &node : effect.nodes) {
    if (node.name != name) {
      continue;
    }
    VfxMotion &motion = node.motion;
    if (type == "KillEntity") {
      motion.kill = static_cast<float>(modifier.number_or("StartTime", 0.0)) * seconds_per_ms;
      effect.length = std::max(effect.length, motion.kill);
    } else if (type == "Rotate") {
      motion.spin = vec3_or(modifier, "Rotation Speed", {});
    } else if (type == "Scale") {
      motion.scale_min = vec3_or(modifier, "MinScale", motion.scale_min);
      motion.scale_max = vec3_or(modifier, "MaxScale", motion.scale_max);
      motion.scale_delta = vec3_or(modifier, "ScaleDelta", {});
    } else if (type == "Blend") {
      motion.alpha_rate = static_cast<float>(modifier.number_or("AlphaLevelDelta", 0.0));
    }
  }
}

} // namespace

VfxData parse_vfx(const json::JsonValue &document) {
  VfxData data;
  data.enabled = document.boolean_or("enabled", false);
  data.unit_scale = static_cast<float>(document.number_or("unit_scale", data.unit_scale));
  if (const auto *weapons = document.find("weapons"); weapons != nullptr) {
    for (const auto &[key, value] : weapons->object()) {
      data.weapons[key] = {value.string_or("fire", ""), value.string_or("shot", ""),
                           value.string_or("hit", "")};
    }
  }
  if (const auto *effects = document.find("effects"); effects != nullptr) {
    for (const auto &[key, value] : effects->object()) {
      VfxEffect &effect = data.effects[key];
      for (const auto &node : value.find("nodes")->array()) {
        effect.nodes.push_back(parse_node(node));
      }
      for (const auto &modifier : value.find("modifiers")->array()) {
        apply(effect, modifier);
      }
    }
  }
  return data;
}

} // namespace phantomcrash
