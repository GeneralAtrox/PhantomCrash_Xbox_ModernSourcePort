#include "phantomcrash/rumbling_map.hpp"

#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/math3d.hpp"

#include <cmath>

namespace phantomcrash {

using json::numbers;
using json::required;

RumblingMapProjection parse_rumbling_map_projection(const json::JsonValue &value) {
  RumblingMapProjection map;
  map.offset = numbers<2>(required(value, "offset"), "offset");
  map.pivot = numbers<2>(required(value, "pivot"), "pivot");
  map.angle = static_cast<float>(value.number_or("angle", map.angle));
  map.scale = static_cast<float>(value.number_or("scale", map.scale));
  return map;
}

RumblingRobotMarkers parse_rumbling_robot_markers(const json::JsonValue &value) {
  RumblingRobotMarkers markers;
  markers.present = true;
  markers.origin = numbers<2>(required(value, "origin"), "origin");
  markers.colour = parse_colour(required(value, "colour").string());
  markers.chosen_colour = parse_colour(required(value, "chosen_colour").string());
  markers.ring = parse_sprite(required(value, "ring"));
  markers.ring_degrees_per_second =
      static_cast<float>(required(value, "ring_degrees_per_second").number());
  markers.fewest = static_cast<std::size_t>(value.number_or("fewest", 2.0));
  for (const json::JsonValue &icon : required(value, "icons").array()) {
    const int maker = static_cast<int>(required(icon, "maker").number());
    const int legs = static_cast<int>(required(icon, "legs_type").number());
    markers.icons[{maker, legs}] = parse_sprite(required(icon, "sprite"));
  }
  return markers;
}

// 0x04438d: the stage point scaled onto the map (z running up it), turned
// about the pivot by the area's angle.
std::array<float, 2> rumbling_map_point(const RumblingRobotMarkers &markers,
                                        const RumblingMapProjection &map, const float x,
                                        const float z) {
  const float across = x * map.scale + map.offset[0] - map.pivot[0];
  const float down = -z * map.scale + map.offset[1] - map.pivot[1];
  const float c = std::cos(map.angle);
  const float s = std::sin(map.angle);
  return {markers.origin[0] + map.pivot[0] + across * c + down * s,
          markers.origin[1] + map.pivot[1] - across * s + down * c};
}

// 0x04407a: -angle - pi/2 - atan2(forward z, forward x), forward being
// (-sin, -cos) of the heading.
float rumbling_map_degrees(const RumblingMapProjection &map, const float heading) {
  const float forward = std::atan2(-std::cos(heading), -std::sin(heading));
  const float radians = -map.angle - pi * 0.5F - forward;
  return radians * 180.0F / pi;
}

} // namespace phantomcrash
