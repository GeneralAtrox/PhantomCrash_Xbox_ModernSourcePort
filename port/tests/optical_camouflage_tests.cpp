// Optic Camouflage's sphere: where it stands through the fade, what it
// shows and how far it bends the frame behind the robot.

#include "test_support.hpp"

#include <cmath>

#include "phantomcrash/json.hpp"
#include "phantomcrash/optical_camouflage.hpp"

using namespace phantomcrash;

namespace {

bool near(const float a, const float b) {
  return std::fabs(a - b) < 0.001F;
}

} // namespace

TEST_CASE("the camouflage sphere rises from the feet over the fade") {
  const OpticalCamouflage look;
  const Vec3 feet{2.0F, 1.0F, -3.0F};
  CHECK(near(optical_sphere(look, feet, 0.0F).centre.y, 1.0F));
  CHECK(near(optical_sphere(look, feet, 0.5F).centre.y, 6.0F));
  const OpticalSphere on = optical_sphere(look, feet, 1.0F);
  CHECK(near(on.centre.y, 11.0F) && near(on.centre.x, 2.0F));
  // Off, a robot's body stands inside the sphere and is seen; on, it is
  // below the sphere and shows the frame behind it.
  const Vec3 chest = feet + Vec3{0.0F, 2.5F, 0.0F};
  CHECK(near(optical_seen(optical_sphere(look, feet, 0.0F), chest), 1.0F));
  CHECK(near(optical_seen(on, chest), 0.0F));
}

TEST_CASE("the camouflage's edge is soft and its push fades to a least") {
  const OpticalCamouflage look;
  const OpticalSphere sphere = optical_sphere(look, {}, 0.0F);
  CHECK(near(optical_seen(sphere, {4.5F, 0.0F, 0.0F}), 0.5F));
  CHECK(near(optical_push(sphere, {5.0F, 0.0F, 0.0F}), 1.0F));
  CHECK(near(optical_push(sphere, {7.0F, 0.0F, 0.0F}), 0.5F));
  CHECK(near(optical_push(sphere, {20.0F, 0.0F, 0.0F}), 0.25F));
  const OpticalCamouflage parsed =
      parse_optical_camouflage(json::parse(R"({"radius": 3, "rise": 8})"));
  CHECK(near(parsed.radius, 3.0F) && near(parsed.rise, 8.0F) && near(parsed.reach, 4.0F));
}
