#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"

namespace phantomcrash {

// Optic Camouflage's look, from the shaders on the disc (Media/Shaders/
// optical.xvu, multi_optical.xvu and Blend.xpu) and their setup (0x0c05c2,
// 0x0c21db). Every robot vertex is measured against a sphere: inside it the
// robot is seen as usual; outside it the robot shows the frame behind it,
// sampled where the vertex would land pushed out along its normal, so the
// background bends around the robot's shape (a refraction). While the
// camouflage turns on, the sphere's centre rises from the robot's base by
// `rise` (0x074e80 stores it per robot in the table at 0xa94640), so the
// robot vanishes from the bottom up; turning off lowers it again.
struct OpticalCamouflage {
  float rise{10.0F};  // the centre's climb over the fade
  float radius{5.0F}; // c8.w: the robot is seen within this distance
  float edge{1.0F};   // 1 / c9.y: the width of the seen part's soft edge
  float reach{4.0F};  // c9.x - c8.w: the push fades out this far past the radius
  float push{1.0F};   // the push along the normal near the sphere
  float least{0.25F}; // c4.y: the smallest push, however far away
};

// One robot's sphere, as the renderer takes it.
struct OpticalSphere {
  Vec3 centre;
  float radius{};
  float edge{1.0F};
  float reach{1.0F};
  float push{};
  float least{};
};

// data/world/training.json "optical_camouflage"; missing values keep the
// original's.
[[nodiscard]] OpticalCamouflage parse_optical_camouflage(const json::JsonValue &value);

// The sphere of a robot whose base (robot +0x454c) is `base`, `sweep` of
// the way through its fade (0 seen, 1 fully camouflaged).
[[nodiscard]] OpticalSphere optical_sphere(const OpticalCamouflage &look, Vec3 base, float sweep);

// How much of the robot is seen at `point` (1 inside the sphere, 0 outside
// its edge), and how far `point` is pushed along its normal to pick the
// frame behind it.
[[nodiscard]] float optical_seen(const OpticalSphere &sphere, Vec3 point);
[[nodiscard]] float optical_push(const OpticalSphere &sphere, Vec3 point);

} // namespace phantomcrash
