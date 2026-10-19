#include "test_support.hpp"
#include "phantomcrash/robot_library.hpp"
#include <cmath>

using namespace phantomcrash;

namespace {
constexpr float source_pi = 3.1415927410125732F;
SkeletonNode rig() {
  SkeletonNode root;
  // Deliberately oppose source registration order to tree traversal order.
  for (const int track : {2, 1, 3}) {
    SkeletonNode node;
    node.track = track;
    node.bone = track;
    node.flags = node_wheel;
    node.translation = {track == 1 ? -1.0F : 1.0F, 1, 2};
    node.rotation = {.2F, .4F, .7F};
    node.source_roll =
        SourceRollControl{track == 3 ? 1 : 0, track == 3 ? 1.0F : .5F, source_pi, 3.1677728F, 1};
    root.children.push_back(node);
  }
  return root;
}
Pose pose(const SkeletonNode &skeleton, const RobotMover &mover) {
  PoseInput input;
  input.root = rotation_y(mover.heading()) * translation(mover.position());
  Pose out;
  build_pose(skeleton, input, out);
  return out;
}
void advance(SourceRollState &state, const SkeletonNode &skeleton, const RobotMover &mover) {
  const auto p = pose(skeleton, mover);
  state.advance(skeleton, p[0].bones, p[0].set, mover, 1.0F / 60);
}
} // namespace

TEST_CASE("source wheels retain callback order shared accumulators and separate coefficients") {
  const auto skeleton = rig();
  RobotMover mover(MovementTuning{});
  mover.place({}, 0);
  SourceRollState state;
  advance(state, skeleton, mover);
  mover.set_position({0, 0, -.1F});
  advance(state, skeleton, mover);
  const float step = -.2F / source_pi;
  CHECK(std::abs(state.angles()[1] - step) < 1e-6F);
  CHECK(std::abs(state.angles()[2] - 2 * step) < 1e-6F);
  CHECK(std::abs(state.angles()[3] - step / 2) < 1e-6F);
  mover.place({0, 0, -.1F}, .2F);
  advance(state, skeleton, mover);
  CHECK(std::abs(state.angles()[1] - 2 * step) < 1e-6F);
  CHECK(std::abs(state.angles()[2] - 2 * step) < 1e-6F);
  CHECK(std::abs(state.angles()[3] - step / 2) < 1e-6F);
  ScriptedStep still;
  still.heading = .2F;
  mover.script(still);
  RobotControl steer;
  steer.move[0] = .5F;
  mover.tick(steer, 1.0F / 60);
  advance(state, skeleton, mover);
  const float turning = radians(110.0F) * .5F / 60;
  CHECK(std::abs(state.angles()[1] - (2 * step + turning)) < 1e-6F);
  CHECK(std::abs(state.angles()[2] - 2 * step) < 1e-6F);
  CHECK(std::abs(state.angles()[3] - (step / 2 - turning)) < 1e-6F);
}

TEST_CASE("source wheel travel uses the actor heading and spin preserves mesh position") {
  const auto skeleton = rig();
  RobotMover mover(MovementTuning{});
  mover.place({0, 0, 0}, source_pi / 2);
  SourceRollState state;
  advance(state, skeleton, mover);
  mover.set_position({-.1F, 0, 0});
  advance(state, skeleton, mover);
  CHECK(std::abs(state.angles()[1] + .2F / source_pi) < 1e-6F);
  const auto bind = pose(skeleton, mover);
  PoseInput input;
  input.root = rotation_y(mover.heading()) * translation(mover.position());
  input.source_roll_angles = &state.angles();
  input.wheel_turn = 123; // The native scalar must not also spin imported controls.
  Pose out, repeated;
  build_pose(skeleton, input, out);
  build_pose(skeleton, input, repeated);
  for (const auto &node : skeleton.children) {
    const auto &matrix = out[0].bones[static_cast<std::size_t>(node.bone)];
    const auto original = bind[0].bones[static_cast<std::size_t>(node.bone)];
    for (int axis = 0; axis < 3; ++axis) {
      CHECK(std::abs(matrix.at(3, axis) - original.at(3, axis)) < 1e-6F);
      CHECK(std::abs(matrix.at(0, axis) - original.at(0, axis)) < 1e-6F);
    }
    CHECK(matrix.m == repeated[0].bones[static_cast<std::size_t>(node.bone)].m);
  }
}

TEST_CASE("source wheel steps clamp wrap and freeze when the actor is destroyed") {
  const auto leaf = rig().children[1];
  RobotMover mover(MovementTuning{});
  mover.place({}, 0);
  SourceRollState state;
  advance(state, leaf, mover);
  mover.set_position({0, 0, -100});
  advance(state, leaf, mover);
  const float clamped = 2 * source_pi - 3.1677728F;
  CHECK(std::abs(state.angles()[1] - clamped) < 1e-6F);
  mover.destroy();
  mover.set_position({0, 0, 100});
  advance(state, leaf, mover);
  CHECK(std::abs(state.angles()[1] - clamped) < 1e-6F);
}

TEST_CASE("airborne source wheels use forward control instead of ground displacement") {
  const auto leaf = rig().children[1];
  RobotMover mover(MovementTuning{});
  mover.place({}, 0);
  SourceRollState state;
  advance(state, leaf, mover);
  mover.set_floor(-1000);
  RobotControl control;
  control.move = {0, 1};
  mover.tick(control, 1.0F / 60);
  REQUIRE(!mover.grounded());
  mover.set_position({0, 20, 100}); // Travel direction must not change airborne roll.
  advance(state, leaf, mover);
  CHECK(std::abs(state.angles()[1] + 1 / source_pi) < 1e-6F);
}

TEST_CASE("source spherical rolling accumulates all axes and replaces the bind basis") {
  auto sphere = rig().children[1];
  sphere.source_roll =
      SourceRollControl{0, 1 / source_pi, source_pi, 3.1677728F, 1, true, {1.025F, 1.025F, 1.025F}};
  RobotMover mover(MovementTuning{});
  mover.place({}, 0);
  SourceRollState state;
  advance(state, sphere, mover);
  REQUIRE(state.frames()[1].has_value());
  mover.set_position({0, 0, -source_pi / 2});
  advance(state, sphere, mover);
  mover.set_position({-source_pi / 2, 0, -source_pi / 2});
  advance(state, sphere, mover);
  const auto sample = [&]() {
    PoseInput input;
    input.root = rotation_y(mover.heading()) * translation(mover.position());
    input.source_roll_angles = &state.angles();
    input.source_roll_frames = &state.frames();
    input.wheel_turn = 123; // Spherical controls must not also receive scalar roll.
    Pose out;
    build_pose(sphere, input, out);
    return out[0].bones[1];
  };
  const auto world = sample();
  const auto bind = pose(sphere, mover)[0].bones[1];
  const Vec3 origin = transform_point({}, world);
  CHECK(length(origin - transform_point({}, bind)) < 1e-5F);
  // A forward quarter-turn followed by a sideways quarter-turn is not an Euler sum.
  CHECK(length(transform_point({1.025F, 0, 0}, world) - origin - Vec3{0, 0, -1}) < 1e-5F);
  CHECK(length(transform_point({0, 1.025F, 0}, world) - origin - Vec3{-1, 0, 0}) < 1e-5F);
  CHECK(length(transform_point({0, 0, 1.025F}, world) - origin - Vec3{0, 1, 0}) < 1e-5F);
  CHECK(world.m == sample().m);
  const auto before_reposition = *state.frames()[1];
  mover.place(mover.position(), source_pi / 2);
  advance(state, sphere, mover);
  CHECK(state.frames()[1]->m == before_reposition.m);
  const auto without_input = *state.frames()[1];
  CHECK(length(transform_point({0, 1, 0}, without_input) - Vec3{-1, 0, 0}) < 1e-5F);
  auto tuning = mover.tuning();
  tuning.turn_degrees_per_second = 90 * 60;
  mover.set_tuning(tuning);
  ScriptedStep still;
  still.heading = source_pi / 2;
  mover.script(still);
  RobotControl steer;
  steer.move[0] = 1;
  mover.tick(steer, 1.0F / 60);
  advance(state, sphere, mover);
  const auto steered = *state.frames()[1];
  CHECK(length(transform_point({1, 0, 0}, steered) - Vec3{0, 0, -1}) < 1e-5F);
  CHECK(length(transform_point({0, 1, 0}, steered) - Vec3{0, 1, 0}) < 1e-5F);
  CHECK(length(transform_point({0, 0, 1}, steered) - Vec3{1, 0, 0}) < 1e-5F);
  mover.destroy();
  mover.set_position({100, 200, 300});
  advance(state, sphere, mover);
  CHECK(state.frames()[1]->m == steered.m);
}

TEST_CASE("spherical steering cancels clock ticks while single-axis steering retains them") {
  auto single = rig().children[1];
  auto sphere = single;
  sphere.source_roll->spherical = true;
  MovementTuning tuning;
  tuning.turn_degrees_per_second = 600;
  tuning.wheels = WheeledTuning{1, 1, 1, 7, .25F, 1};
  const auto run = [&](const SkeletonNode &node, const float seconds) {
    RobotMover mover(tuning);
    mover.place({}, 0);
    mover.script({}); // Hold the chassis; direct steering must still reach the callback.
    SourceRollState state;
    RobotControl input;
    input.strafe = .5F; // Also exercise the wheeled lateral-binding adapter.
    mover.tick(input, seconds);
    const auto p = pose(node, mover);
    state.advance(node, p[0].bones, p[0].set, mover, seconds);
    CHECK(mover.heading() == 0);
    return state;
  };
  const auto one = run(single, 1.0F / 60);
  const auto two = run(single, 1.0F / 30);
  CHECK(std::abs(two.angles()[1] - 2 * one.angles()[1]) < 1e-6F);
  CHECK(std::abs(one.angles()[1] - radians(5.0F)) < 1e-6F);
  const auto sphere_one = run(sphere, 1.0F / 60);
  const auto sphere_two = run(sphere, 1.0F / 30);
  CHECK(sphere_one.frames()[1]->m == sphere_two.frames()[1]->m);
  const auto rotated = transform_point({1, 0, 0}, *sphere_one.frames()[1]);
  CHECK(length(rotated - Vec3{std::cos(radians(5.0F)), -std::sin(radians(5.0F)), 0}) < 1e-6F);
}

TEST_CASE("source spherical rolling ignores airborne lateral displacement and resets its matrix") {
  auto sphere = rig().children[1];
  sphere.source_roll = SourceRollControl{0, 1 / source_pi, source_pi, 3.1677728F, 1, true};
  RobotMover mover(MovementTuning{});
  mover.place({}, 0);
  SourceRollState state;
  advance(state, sphere, mover);
  mover.set_floor(-1000);
  RobotControl control;
  control.move = {0, 1};
  mover.tick(control, 1.0F / 60);
  REQUIRE(!mover.grounded());
  mover.set_position({100, 200, 300});
  advance(state, sphere, mover);
  const auto frame = *state.frames()[1];
  CHECK(length(transform_point({1, 0, 0}, frame) - Vec3{1, 0, 0}) < 1e-5F);
  // A 1/60-second sample is half of the native 30 Hz movement-input tick.
  CHECK(length(transform_point({0, 1, 0}, frame) - Vec3{0, std::cos(.5F), -std::sin(.5F)}) < 1e-5F);
  state = SourceRollState{};
  CHECK(!state.frames()[1].has_value());
  mover.set_floor(0);
  mover.place({}, 0);
  advance(state, sphere, mover);
  CHECK(state.frames()[1]->m == Mat4{}.m);
}
