// Robot part tables and skeletons from the European default.xbe (robots.py).
// See docs/ROBOTS.md. A robot is a body, legs, two arms and two shoulder
// weapons; each part record's type picks its model, skeleton and motions.
#pragma once

#include "py_json.hpp"
#include "xbe.hpp"

#include <cstdint>
#include <set>

namespace phantomcrash::extract {

// {"parts", "body_types", "leg_types", "arm_types", "shoulder_types",
// "emblems"}: every part's type and every type's models, skeletons,
// motions and paints.
[[nodiscard]] Json robot_catalogue(const XbeImage &image);

// {"legs": [...], "arms": [[...], [...]]}: the static skeleton trees.
[[nodiscard]] Json robot_skeletons(const XbeImage &image);

struct RobotResources {
  std::set<std::uint32_t> models, textures, motions;
};

// The model, texture and motion resources the catalogue names.
[[nodiscard]] RobotResources referenced_resources(const Json &catalogue);

// A JSON integer (signed or unsigned) as a 64-bit value.
[[nodiscard]] std::int64_t json_int(const Json &value);

} // namespace phantomcrash::extract
