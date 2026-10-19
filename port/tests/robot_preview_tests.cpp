// The garage's robot preview (0x0836a1 .. 0x084831): its camera, where the
// robot stands and the parts shown one by one.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/robot_preview.hpp"

#include <cmath>

using namespace phantomcrash;

namespace {

bool near(const float a, const float b) {
  return std::fabs(a - b) <= 1e-4F;
}

RobotPreviewScript shipped() {
  return parse_robot_preview(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/robot_preview.json"));
}

} // namespace

TEST_CASE("robot_preview.json reads the original's camera, light and places") {
  const RobotPreviewScript script = shipped();
  CHECK(near(script.eye.z, -12.0F) && near(script.target.y, 2.0F));
  CHECK(near(script.fov_degrees, 52.0F));
  CHECK(!script.animate); // the standing robot holds its first frame (0x0574f5)
  CHECK(near(script.ambient, 0.25F) && near(script.diffuse, 1.0F));
  REQUIRE(script.screens.contains("sv_spec"));
  const RobotPreviewScreen &sv = script.screens.at("sv_spec");
  CHECK(near(sv.place.position.x, 4.0F) && near(sv.place.position.y, -0.2F));
  CHECK(near(sv.place.yaw, 0.4F));
  CHECK(near(length(sv.light_direction), 1.0F));
  // The hangar has a light of its own.
  CHECK(!near(script.screens.at("hangar").light_direction.x, sv.light_direction.x));
  CHECK(near(script.part_display[3].position.x, 1.15F)); // the left arm
  CHECK_THROWS(parse_robot_preview(json::parse(R"({"layer": 1})")));
}

TEST_CASE("a hangar's robot is its fitted stock, arms and shoulders by side") {
  Garage garage;
  garage.stock[0] = {PartKind::body, 12};
  garage.stock[1] = {PartKind::legs, 3};
  garage.stock[4] = {PartKind::arm, 7};
  Hangar hangar;
  hangar.parts[static_cast<std::size_t>(PartSlot::body)] = 0;
  hangar.parts[static_cast<std::size_t>(PartSlot::legs)] = 1;
  hangar.parts[static_cast<std::size_t>(PartSlot::left_arm)] = 4;
  hangar.paint = 9;
  const PreviewRobot robot = preview_robot(garage, hangar);
  CHECK(robot.body == 12);
  CHECK(robot.legs == 3);
  CHECK(robot.arms[0] == -1);
  CHECK(robot.arms[1] == 7);
  CHECK(robot.colour == 9);
  CHECK(robot.standing());
  CHECK(!preview_part(PartKind::body, 12).standing());
  CHECK(preview_part(PartKind::arm, 5).arms[0] == 5);
}

TEST_CASE("a robot without legs shows its parts one by one about its place") {
  RobotCatalogue catalogue;
  catalogue.body_parts = {0};
  catalogue.body_types = {{40}};
  catalogue.arm_parts = {0};
  catalogue.arm_types = {{{50, 51}}};
  const RobotPreviewScript script = shipped();
  const RobotPreviewScreen &screen = script.screens.at("sv_spec");
  const PreviewPlacement &place = screen.place;
  PreviewRobot robot;
  robot.body = 0;
  robot.arms = {-1, 0};
  robot.legs = 9; // unknown: left out
  const auto parts = preview_part_display(catalogue, script, robot);
  REQUIRE(parts.size() == 2);
  CHECK(parts[0].model == robot_model_path(40));
  CHECK(parts[1].model == robot_model_path(51)); // the left arm's own model
  const Vec3 body = transform_point({}, parts[0].local * preview_root(screen, 0.0));
  CHECK(near(body.y, place.position.y + 2.3F));
  const Vec3 across = body - place.position;
  CHECK(near(std::hypot(across.x, across.z), 0.05F));
  const Vec3 root = transform_point({}, preview_root(screen, 5.0));
  CHECK(near(root.x, place.position.x) && near(root.z, place.position.z));
  // It turns half a degree a frame (0x0574f5): the body swings about the root.
  const Vec3 later = transform_point({}, parts[0].local * preview_root(screen, 3.0));
  CHECK(near(later.y, body.y));
  CHECK(!near(later.x, body.x));
  // Parts shown alone are not given the spin (0x0842c6).
  const Vec3 still = transform_point({}, parts[0].local * preview_root(screen, 3.0, false));
  CHECK(near(still.x, body.x) && near(still.z, body.z));
  CHECK(near(screen.spin, radians(0.5F) * 60.0F));
  CHECK(near(script.screens.at("stock_module").spin, 0.0F));
  for (const char *shop : {"dealer", "module_shop", "tune_shop"}) {
    CHECK(script.screens.contains(shop));
  }
}

TEST_CASE("a shop's lone part takes its model type's place and pops in") {
  RobotCatalogue catalogue;
  catalogue.body_parts = {0};
  catalogue.body_types = {{40}};
  const RobotPreviewScript script = shipped();
  const RobotPreviewScreen &screen = script.screens.at("module_shop");
  const auto parts = preview_part_display(catalogue, script, preview_part(PartKind::body, 0));
  REQUIRE(parts.size() == 1);
  // Body type 0 takes place 0 (0x2427a4): (0.5, 2.65, -3.24) from the root.
  const Mat4 root = preview_root(screen, screen.place.position, 0.0, false);
  const Vec3 at = transform_point({}, parts[0].local * root);
  CHECK(near(at.y, screen.place.position.y + 2.65F));
  const Vec3 across = at - screen.place.position;
  CHECK(near(std::hypot(across.x, across.z), std::hypot(0.5F, 3.24F)));
  CHECK(at.z < screen.place.position.z); // nearer the camera than the root
  // Frame 0 is not drawn; it is full size after 15 frames (0x083b0b).
  CHECK(near(lone_part_scale(script.lone_part, 0.0), 0.0F));
  CHECK(near(lone_part_scale(script.lone_part, 1.0 / 60.0), 1.0F / 15.0F));
  CHECK(near(lone_part_scale(script.lone_part, 1.0), 1.0F));
  const Vec3 pivot = transform_point({}, translation(script.lone_part.pop_in_pivot) *
                                             lone_part_pop_in(script.lone_part, 0.5F));
  CHECK(near(pivot.y, 4.0F)); // it grows about the pivot
  // A long part is cut at the shop's box rather than spilling out of it.
  REQUIRE(screen.part_clip.has_value());
  CHECK((*screen.part_clip)[0] < (*screen.part_clip)[2]);
  CHECK(!script.screens.at("hangar").part_clip.has_value());
}

TEST_CASE("a standing robot stands by its legs' type and slides in at the shops") {
  RobotCatalogue catalogue;
  catalogue.body_parts = {0};
  catalogue.leg_parts = {5, 0};
  const RobotPreviewScript script = shipped();
  const RobotPreviewScreen &dealer = script.screens.at("dealer");
  PreviewRobot robot;
  robot.body = 0;
  robot.legs = 0; // type 5
  const Vec3 spot = preview_spot(catalogue, script, dealer, robot);
  CHECK(near(spot.x, 1.4F) && near(spot.y, 0.2F) && near(spot.z, -5.0F));
  // It starts 8 to the right (0x083f17) and closes 5% a frame (0x0574f5).
  CHECK(near(transform_point({}, preview_root(dealer, spot, 0.0, true)).x, spot.x + 8.0F));
  const Vec3 frame = transform_point({}, preview_root(dealer, spot, 1.0 / 60.0, true));
  CHECK(near(frame.x, spot.x + 8.0F * 0.95F));
  CHECK(std::fabs(transform_point({}, preview_root(dealer, spot, 10.0, true)).x - spot.x) < 1e-3F);
  // Screens without leg places stand it at their position.
  const RobotPreviewScreen &hangar = script.screens.at("hangar");
  CHECK(near(preview_spot(catalogue, script, hangar, robot).x, hangar.place.position.x));
}
