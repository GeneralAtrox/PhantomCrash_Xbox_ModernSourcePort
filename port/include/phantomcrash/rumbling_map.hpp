#pragma once

// The robots on RUMBLING's area map (data/ui/rumbling.json "robot_markers"
// and each area's "robot_map"): 0x01492b draws an icon for each robot out
// through 0x04407a and 0x04438d, by its maker and legs type, turned the way
// it faces. The live RUMBLING (rumbling_world.hpp) gives where they stand.

#include "phantomcrash/json.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <utility>

namespace phantomcrash {

// How an area's stage lies on its map (0x1d6fb0, 0x14 bytes an area, and
// the scale at 0x1d70d0).
struct RumblingMapProjection {
  std::array<float, 2> offset{};
  std::array<float, 2> pivot{}; // the map turns about this point
  float angle{};                // radians
  float scale{1.0F};
};

struct RumblingRobotMarkers {
  bool present{};
  std::array<float, 2> origin{}; // the map's (0x17beb8)
  std::uint32_t colour{0xffffff};
  std::uint32_t chosen_colour{0xffffff}; // the one CHARACTER INFO shows
  Sprite ring;                           // turning round the chosen one
  float ring_degrees_per_second{};
  std::size_t fewest{2}; // fewer robots out than this show none
  // By maker and legs type (0x200838, nine legs types a maker).
  std::map<std::pair<int, int>, Sprite> icons;
};

[[nodiscard]] RumblingMapProjection parse_rumbling_map_projection(const json::JsonValue &value);
[[nodiscard]] RumblingRobotMarkers parse_rumbling_robot_markers(const json::JsonValue &value);

// Where a point of the stage (x, z) is on the map.
[[nodiscard]] std::array<float, 2> rumbling_map_point(const RumblingRobotMarkers &markers,
                                                      const RumblingMapProjection &map, float x,
                                                      float z);
// The clockwise degrees an icon turns for a robot at `heading` (0 facing -z).
[[nodiscard]] float rumbling_map_degrees(const RumblingMapProjection &map, float heading);

} // namespace phantomcrash
