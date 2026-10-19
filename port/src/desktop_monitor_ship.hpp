#pragma once

#include "desktop_light_sources.hpp"
#include "desktop_world_renderer.hpp"

namespace phantomcrash {

// The arena's advertising ship, with the same live feed as the gate TVs.
// Authored model parts and flight curves come from tables/arena_monitor.json.
class MonitorShip final {
public:
  MonitorShip(WorldRenderer &world, const DataRoots &roots, int stage);
  void tick(float seconds);
  void add_items(std::vector<WorldItem> &items) const;
  void add_lights(std::vector<WorldLight> &lights) const;

private:
  struct Part {
    int node{-1};
    int mesh{-1};
    std::string name;
    bool screen{};
    float hinge_x{};
    float hinge_sign{};
    bool spinner{};
    Vec3 pivot;
  };
  int model_{-1};
  std::vector<AttachedLight> lights_;
  std::vector<Part> parts_;
  std::vector<std::array<Vec3, 4>> route_;
  std::vector<int> open_segments_, close_segments_;
  float speed_{}, hinge_speed_{}, spin_speed_{}, hinge_max_{};
  float along_{}, hinge_{}, spin_{};
  int segment_{}, hinge_direction_{};
  Mat4 world_;

  // Where a part is drawn this frame: hinged, spinning or with the hull.
  [[nodiscard]] Mat4 part_world(const Part &part) const;
};

} // namespace phantomcrash
