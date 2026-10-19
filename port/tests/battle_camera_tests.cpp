// The battle camera's views: the D-pad's order through them, the blend
// between first person and behind, where each view stands, and the fixed
// cameras' pick.

#include "test_support.hpp"

#include <cmath>
#include <vector>

#include "phantomcrash/battle_camera.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/robot_mover.hpp"

using namespace phantomcrash;

namespace {

constexpr float tick = 1.0F / 30.0F;

bool near(const Vec3 a, const Vec3 b) {
  return length(a - b) < 0.01F;
}

// A robot at the origin facing -z, its torso 3 up.
CameraSubject subject() {
  CameraSubject s;
  s.height = 1.5F;
  s.torso = {0.0F, 3.0F, 0.0F};
  s.legs_model = 3;
  return s;
}

const CameraShot chase{{0.0F, 5.0F, 10.0F}, {0.0F, 3.0F, -8.0F}, 55.0F, 0.0F};

} // namespace

// A robot standing at `feet`, its torso 3 up.
CameraSubject standing(const Vec3 feet, const float yaw = 0.0F) {
  CameraSubject s = subject();
  s.position = feet;
  s.torso = feet + Vec3{0.0F, 3.0F, 0.0F};
  s.yaw = yaw;
  return s;
}

TEST_CASE("the behind view stands over the torso and eases after the robot") {
  ChaseCamera camera(ChaseCameraSettings{});
  const StageCollision stage;
  const Vec3 start{320.0F, 20.0F, -480.0F};
  camera.tick(standing(start), stage, tick, true);
  // 0x07edfa: the eye 6.15 behind the torso and 2 up; the look point 8
  // ahead of the feet and 2 up.
  CHECK(near(camera.eye(), start + Vec3{0.0F, 5.0F, 6.15F}));
  CHECK(near(camera.target(), start + Vec3{0.0F, 2.0F, -8.0F}));

  // The eye closes a third of its gap a tick.
  const Vec3 travel{30.0F, 4.0F, -20.0F};
  const Vec3 before = camera.eye();
  camera.tick(standing(start + travel), stage, tick);
  CHECK(near(camera.eye(), before + travel * (1.0F / 3.0F)));
  const float turn = radians(90.0F);
  for (int frame = 0; frame < 90; ++frame) {
    camera.tick(standing(start + travel, turn), stage, tick);
  }
  const Vec3 forward = heading_forward(turn);
  const Vec3 feet = start + travel;
  CHECK(near(camera.eye(), feet + Vec3{0.0F, 5.0F, 0.0F} - forward * 6.15F));
  CHECK(near(camera.target(), feet + Vec3{0.0F, 2.0F, 0.0F} + forward * 8.0F));
}

TEST_CASE("the behind view rises in the air and keeps off walls") {
  ChaseCamera camera(ChaseCameraSettings{});
  ArenaBounds bounds;
  bounds.half_size = 500.0F;
  const StageCollision stage = StageCollision::box(bounds, 30.0F);
  camera.tick(standing({200.0F, 0.0F, 497.0F}), stage, tick, true);
  CHECK(std::abs(camera.eye().x - 200.0F) < 0.01F);
  CHECK(camera.eye().z < 500.0F);
  CHECK(camera.eye().z > 497.0F);
  CHECK(!stage.segment_hit(camera.anchor(), camera.eye()));
  for (int frame = 0; frame < 90; ++frame) {
    camera.tick(standing({200.0F, 0.0F, 450.0F}), stage, tick);
  }
  CHECK(near(camera.eye(), {200.0F, 5.0F, 456.15F}));

  // Off the ground the eye rises the robot's top less half a leg radius.
  CameraSubject jumping = standing({200.0F, 0.0F, 450.0F});
  jumping.grounded = false;
  camera.tick(jumping, stage, tick, true);
  const ChaseCameraSettings defaults;
  CHECK(std::abs(camera.eye().y - (5.0F + defaults.robot_top - 0.75F)) < 0.01F);
}

TEST_CASE("the D-pad walks the orbit views in the original's order") {
  CameraViews views(CameraViewSettings{}, {});
  CHECK(views.view() == camera_view::behind);
  CHECK(!views.change(ViewChange::behind));
  std::vector<int> order;
  for (int index = 0; index < 5; ++index) {
    CHECK(views.change(ViewChange::next));
    order.push_back(views.view());
  }
  CHECK((order == std::vector<int>{2, 3, 4, 5, 2}));
  order.clear();
  for (int index = 0; index < 4; ++index) {
    CHECK(views.change(ViewChange::previous));
    order.push_back(views.view());
  }
  CHECK((order == std::vector<int>{5, 4, 3, 2}));
  CameraViews from_behind(CameraViewSettings{}, {});
  CHECK(from_behind.change(ViewChange::previous));
  CHECK(from_behind.view() == camera_view::fixed);
}

TEST_CASE("first person and behind blend over twenty ticks and hold the buttons meanwhile") {
  CameraViews views(CameraViewSettings{}, {});
  const CameraSubject robot = subject();
  views.tick(robot, chase, tick, {}, true);
  CHECK(near(views.shot().eye, chase.eye));
  CHECK(views.change(ViewChange::first_person));
  CHECK(views.first_person());
  // Legs model 3: 1.0 above and 1.6 ahead of the torso, looking 24 ahead.
  const Vec3 cockpit{0.0F, 4.0F, -1.6F};
  views.tick(robot, chase, tick, {});
  CHECK(near(views.shot().eye, chase.eye)); // the whole of the old view's share
  CHECK(!views.change(ViewChange::behind));
  for (int index = 0; index < 10; ++index) {
    views.tick(robot, chase, tick, {});
  }
  CHECK(near(views.shot().eye, (chase.eye + cockpit) * 0.5F));
  for (int index = 0; index < 10; ++index) {
    views.tick(robot, chase, tick, {});
  }
  CHECK(near(views.shot().eye, cockpit));
  CHECK(near(views.shot().target, {0.0F, 1.5F, -24.0F}));
  CHECK(views.shot().fov_degrees == 52.0F);
  CHECK(views.change(ViewChange::behind));
}

TEST_CASE("the orbit views stand beside and ahead of the robot and look at its torso") {
  CameraViews views(CameraViewSettings{}, {});
  CameraSubject robot = subject();
  robot.yaw = pi * 0.5F; // facing -x
  robot.pitch = 0.25F;
  views.tick(robot, chase, tick, {}, true);
  CHECK(views.change(ViewChange::next));
  views.tick(robot, chase, tick, {});
  // Local -x is the robot's right.
  CHECK(near(views.shot().eye, Vec3{0.0F, 1.65F, 0.0F} + heading_right(robot.yaw) * 5.5F));
  CHECK(near(views.shot().target, {0.0F, 4.5F, 0.0F}));
  CHECK(views.change(ViewChange::next));
  views.tick(robot, chase, tick, {});
  CHECK(near(views.shot().eye, Vec3{0.0F, 5.5F, 0.0F} - heading_right(robot.yaw) * 8.0F));
  CHECK(views.change(ViewChange::next));
  views.tick(robot, chase, tick, {});
  CHECK(near(views.shot().eye, Vec3{0.0F, 2.15F, 0.0F} + heading_forward(robot.yaw) * 8.0F));
}

TEST_CASE("a fixed camera is the nearest point that sees the robot") {
  const auto document = json::parse(R"({"fixed_cameras": {"6": {"points":
    [[100, 5, 0], [10, 5, 0], [20, 5, 0], [0, 5, 30]], "areas": [[0, 4]]}}})");
  const std::vector<Vec3> points = parse_fixed_cameras(document, 6);
  CHECK(points.size() == 4);
  CHECK(parse_fixed_cameras(document, 2).empty());
  CameraViews views(CameraViewSettings{}, points);
  const CameraSubject robot = subject();
  CHECK(views.change(ViewChange::previous));
  bool wall = true; // a wall hides the nearest point
  const SightTest sees = [&](const Vec3 from, Vec3) { return !(wall && from.x == 10.0F); };
  views.tick(robot, chase, tick, sees);
  CHECK(near(views.shot().eye, {20.0F, 5.0F, 0.0F}));
  wall = false;
  for (int index = 0; index < 29; ++index) {
    views.tick(robot, chase, tick, sees);
  }
  CHECK(near(views.shot().eye, {20.0F, 5.0F, 0.0F})); // it looks again after 30 ticks
  views.tick(robot, chase, tick, sees);
  CHECK(near(views.shot().eye, {10.0F, 5.0F, 0.0F}));
  const auto settings = parse_camera_views(json::parse(R"({"fov_degrees": [40, 52, 52, 52, 52, 60],
    "start_view": 0, "orbits": [{"offset": [1, 2, 3]}, {"offset": [0, 0, 0]},
    {"offset": [0, 0, 0], "pitch_lift": true}]})"));
  CHECK(settings.fov_degrees[0] == 40.0F && settings.fov_degrees[5] == 60.0F);
  CHECK(settings.start_view == camera_view::first_person);
  CHECK(settings.orbits[0].offset.z == 3.0F && settings.orbits[2].pitch_lift);
}
