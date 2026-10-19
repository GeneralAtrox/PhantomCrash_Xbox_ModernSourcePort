// Robots: motion channels, skeleton poses with their leg IK, and the part
// catalogue.

#include "test_support.hpp"

#include <cmath>

#include "phantomcrash/json.hpp"
#include "phantomcrash/motion.hpp"
#include "phantomcrash/robot.hpp"
#include "phantomcrash/skeleton.hpp"

using namespace phantomcrash;

namespace {

bool near(const float a, const float b, const float tolerance = 1e-4F) {
  return std::fabs(a - b) <= tolerance;
}

bool near(const Vec3 a, const Vec3 b, const float tolerance = 1e-3F) {
  return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

Vec3 origin(const Mat4 &m) {
  return {m.at(3, 0), m.at(3, 1), m.at(3, 2)};
}

MotionChannel channel(const ChannelKind kind, std::vector<float> keys, std::vector<float> values) {
  MotionChannel c;
  c.kind = kind;
  c.keys = std::move(keys);
  c.values = std::move(values);
  return c;
}

} // namespace

TEST_CASE("motion channels interpolate like the original's reader") {
  CHECK(near(channel(ChannelKind::constant, {}, {2.5F}).sample(9.0F), 2.5F));
  const MotionChannel linear = channel(ChannelKind::linear, {10, 20}, {0, 10, 30});
  CHECK(near(linear.sample(5.0F), 5.0F));
  CHECK(near(linear.sample(15.0F), 20.0F));
  CHECK(near(linear.sample(99.0F), 30.0F)); // holds past the last key
  const MotionChannel step = channel(ChannelKind::step, {10}, {1, 2});
  CHECK(near(step.sample(9.0F), 1.0F));
  CHECK(near(step.sample(10.5F), 2.0F));
  const MotionChannel smooth = channel(ChannelKind::smooth, {10}, {0, 1});
  CHECK(near(smooth.sample(5.0F), 0.5F));
  CHECK(near(smooth.sample(2.5F), 3 * 0.0625F - 2 * 0.015625F));
  // A full Hermite key with no tangents is the smoothstep; tangents add the
  // (f - 1) * elapsed * ((f - 1) m0 + f m1) term.
  const MotionChannel flat = channel(ChannelKind::hermite, {10}, {0, 0, 1, 0});
  CHECK(near(flat.sample(5.0F), 0.5F));
  const MotionChannel sloped = channel(ChannelKind::hermite, {10}, {0, 1, 1, 0});
  CHECK(near(sloped.sample(5.0F), 0.5F + (-0.5F) * 5.0F * (-0.5F)));
}

TEST_CASE("motion banks parse and sample their tracks") {
  const auto bank = parse_motion_bank(json::parse(R"({"track_count": 1, "motions": [null,
    {"frames": 10, "tracks": [{"flags": 227, "translation": [
      {"kind": "constant", "values": [1]},
      {"kind": "linear", "keys": [10], "values": [0, 4]},
      {"kind": "constant", "values": [0]}]}]}]})"));
  CHECK(bank.motion(0) == nullptr);
  const Motion *motion = bank.motion(1);
  CHECK(motion != nullptr);
  const TrackSample sample = motion->sample(0, 5.0F);
  CHECK(!sample.rotation && sample.translation);
  CHECK(near(*sample.translation, {1.0F, 2.0F, 0.0F}));
  CHECK_THROWS(parse_motion_bank(json::parse(R"({"track_count": 2, "motions": [
    {"frames": 1, "tracks": []}]})")));
}

TEST_CASE("a static skeleton places its bones with the game's axis conventions") {
  // Root at (0, 2, 0); a child moved 1 along stored -Z (game +Z) and turned
  // a quarter about Y (stored -pi/2: the evaluator negates X and Y angles).
  const SkeletonNode root = parse_skeleton_node(json::parse(R"({"type": 0, "track": -1,
    "translation": [0, 2, 0], "rotation": [0, 0, 0], "bone": 0, "children": [
      {"type": 0, "track": -1, "translation": [0, 0, -1], "rotation": [0, -1.5707963, 0],
       "bone": 1}]})"));
  PoseInput input;
  Pose pose;
  build_pose(root, input, pose);
  CHECK(pose[0].set[0] && pose[0].set[1] && !pose[0].set[2]);
  CHECK(near(origin(pose[0].bones[0]), {0.0F, 2.0F, 0.0F}));
  CHECK(near(origin(pose[0].bones[1]), {0.0F, 2.0F, 1.0F}));
  // Rotation Y(+pi/2), D3DX: the child's X axis points along -Z.
  CHECK(near(transform_point({1, 0, 0}, pose[0].bones[1]), {0.0F, 2.0F, 0.0F}));
}

TEST_CASE("a forearm turns about X by its body type's turn") {
  // Flag 0x10 marks a forearm (0x0aa499); a Photon body turns it by -pi/2.
  const SkeletonNode root = parse_skeleton_node(json::parse(R"({"type": 0, "track": -1,
    "translation": [0, 0, 0], "rotation": [0, 0, 0], "bone": 0, "flags": 16})"));
  PoseInput input;
  Pose pose;
  build_pose(root, input, pose);
  CHECK(near(transform_point({0, 1, 0}, pose[0].bones[0]), {0.0F, 1.0F, 0.0F}));
  input.forearm_turn = -1.5707963F;
  build_pose(root, input, pose);
  CHECK(near(transform_point({0, 1, 0}, pose[0].bones[0]), {0.0F, 0.0F, -1.0F}));
}

TEST_CASE("leg chains aim one bone and bend two bones to reach their track") {
  // Hip chain at the origin; one bone of 1 aims at track 0, then two bones
  // of 1 reach track 1.
  const SkeletonNode root = parse_skeleton_node(json::parse(R"({"type": 2, "track": -1,
    "translation": [0, 0, 0], "rotation": [0, 0, 0], "children": [
      {"type": 3, "translation": [0, 0, 0], "rotation": [0, 0, 0], "bone": 0, "children": [
        {"type": 4, "track": 0, "translation": [1, 0, 0], "rotation": [0, 0, 0], "children": [
          {"type": 2, "translation": [0, 0, 0], "rotation": [0, 0, 0], "children": [
            {"type": 3, "translation": [0, 0, 0], "rotation": [0, 0, -0.5], "bone": 1,
             "children": [
              {"type": 3, "translation": [1, 0, 0], "rotation": [0, 0, 1.0], "bone": 2,
               "children": [
                {"type": 4, "track": 1, "translation": [1, 0, 0], "rotation": [0, 0, 0],
                 "bone": 3}]}]}]}]}]}]})"));
  // Targets as stored: the evaluator flips Y of animated translations.
  const auto bank = parse_motion_bank(json::parse(R"({"track_count": 2, "motions": [
    {"frames": 0, "tracks": [
      {"flags": 227, "translation": [{"kind": "constant", "values": [0]},
        {"kind": "constant", "values": [1]}, {"kind": "constant", "values": [0]}]},
      {"flags": 227, "translation": [{"kind": "constant", "values": [1.2]},
        {"kind": "constant", "values": [1]}, {"kind": "constant", "values": [0]}]}]}]})"));
  PoseInput input;
  input.legs.motion = bank.motion(0);
  Pose pose;
  build_pose(root, input, pose);
  // The thigh points straight down at (0, -1, 0).
  CHECK(near(transform_point({1, 0, 0}, pose[0].bones[0]), {0.0F, -1.0F, 0.0F}));
  // The shin's end sits on the second target, (1.2, -1, 0).
  CHECK(pose[0].set[3]);
  CHECK(near(origin(pose[0].bones[3]), {1.2F, -1.0F, 0.0F}));
  CHECK(near(length(origin(pose[0].bones[2]) - origin(pose[0].bones[1])), 1.0F));
}

TEST_CASE("the catalogue assembles a robot's models, motions and paint") {
  const auto catalogue = parse_robot_catalogue(json::parse(R"({
    "parts": {"body": [0], "legs": [0], "arm": [1], "shoulder": [0]},
    "body_types": [{"model": 38, "emblem_model": 177, "motions": 1722}],
    "leg_types": [{"model": 39, "motions": 1727, "arm_motions": [[1723, 1724], [1725, 1726]],
                   "paints": [383, 384]}],
    "arm_types": [{"models": [9, 0], "class": 0}, {"models": [10, 1], "class": 1}],
    "shoulder_types": [{"models": [28, 18]}],
    "emblems": [1349]})"));
  RobotSkeletons skeletons;
  skeletons.legs.emplace_back(SkeletonNode{});
  skeletons.arms[0].resize(2);
  skeletons.arms[1].resize(2);
  RobotParts parts;
  parts.arms = {0, 0};
  parts.shoulders = {-1, 0};
  parts.colour = 1;
  const RobotAssembly robot = assemble_robot(catalogue, skeletons, parts);
  CHECK(robot.body_model == "models/binlink/38.gltf");
  CHECK(robot.legs_model == "models/binlink/39.gltf");
  CHECK(robot.arm_models[0] == "models/binlink/10.gltf");
  CHECK(robot.arm_models[1] == "models/binlink/1.gltf");
  CHECK(robot.arm_motions[0] == "motions/1724.json"); // arm class 1
  CHECK(robot.shoulder_models[0].empty() && robot.shoulder_models[1] == "models/binlink/18.gltf");
  CHECK(robot.paint == "textures/binlink/384.png");
  CHECK(robot.skeleton != nullptr);
  parts.legs = 3;
  CHECK_THROWS(assemble_robot(catalogue, skeletons, parts));
}

TEST_CASE("drawn nodes skin rigidly to consecutive bone slots") {
  ModelData model;
  model.nodes.resize(2);
  model.draws.resize(2);
  model.draws[0].node = 0;
  model.draws[1].node = 1;
  model.nodes[0].local = translation({1, 0, 0});
  model.nodes[1].local = translation({0, 1, 0});
  model.nodes[1].world = translation({5, 5, 5});
  PosePart bones;
  bones.bones[4] = translation({0, 0, 10});
  bones.set[4] = true;
  std::vector<Mat4> worlds;
  skin_nodes(model, bones, 4, Mat4{}, worlds);
  CHECK(worlds.size() == 2);
  CHECK(near(origin(worlds[0]), {1.0F, 0.0F, 10.0F}));
  CHECK(near(origin(worlds[1]), {5.0F, 5.0F, 5.0F})); // slot 5 is not posed
}

TEST_CASE("a change that interrupts a blend carries the blended pose on") {
  const auto bank = parse_motion_bank(json::parse(R"({"track_count": 1, "motions": [
    {"frames": 1, "tracks": [{"flags": 227, "translation": [{"kind": "constant", "values": [0]},
      {"kind": "constant", "values": [0]}, {"kind": "constant", "values": [0]}]}]},
    {"frames": 1, "tracks": [{"flags": 227, "translation": [{"kind": "constant", "values": [10]},
      {"kind": "constant", "values": [0]}, {"kind": "constant", "values": [0]}]}]},
    {"frames": 1, "tracks": [{"flags": 227, "translation": [{"kind": "constant", "values": [20]},
      {"kind": "constant", "values": [0]}, {"kind": "constant", "values": [0]}]}]}]})"));
  // Motion 0 was halfway into motion 1 when motion 2 began: 2 blends from
  // that half-and-half pose (5), not from motion 1 alone (10).
  MotionSource source;
  source.motion = bank.motion(2);
  source.previous[0] = {bank.motion(0), 0.0F, 1.0F};
  source.previous[1] = {bank.motion(1), 0.0F, 0.5F};
  source.previous_count = 2;
  source.weight = 0.0F;
  CHECK(near(*source.sample(0).translation, {5.0F, 0.0F, 0.0F}));
  source.weight = 0.5F;
  CHECK(near(*source.sample(0).translation, {12.5F, 0.0F, 0.0F}));
  source.weight = 1.0F;
  CHECK(near(*source.sample(0).translation, {20.0F, 0.0F, 0.0F}));
}

TEST_CASE("leaving a dodge does not put its sideways root offset back on the body") {
  // The root-motion node (flags 2). A dodge ends 2 to the side, posed with
  // that offset on the feet; standing keeps its own sway on the body.
  const SkeletonNode root = parse_skeleton_node(json::parse(R"({"type": 0, "track": 0,
    "flags": 2, "translation": [0, 0, 0], "rotation": [0, 0, 0], "bone": 0})"));
  const auto bank = parse_motion_bank(json::parse(R"({"track_count": 1, "motions": [
    {"frames": 1, "tracks": [{"flags": 227, "translation": [{"kind": "constant", "values": [2]},
      {"kind": "constant", "values": [0]}, {"kind": "constant", "values": [0]}]}]},
    {"frames": 1, "tracks": [{"flags": 227, "translation": [{"kind": "constant", "values": [0]},
      {"kind": "constant", "values": [0]}, {"kind": "constant", "values": [0]}]}]}]})"));
  PoseInput input;
  input.keep_sway = true;
  input.legs.motion = bank.motion(1);
  input.legs.previous[0] = {bank.motion(0), 0.0F, 1.0F, false};
  input.legs.previous_count = 1;
  input.legs.weight = 0.5F;
  Pose pose;
  build_pose(root, input, pose);
  CHECK(near(origin(pose[0].bones[0]), {0.0F, 0.0F, 0.0F}));
}

TEST_CASE("another maker's body grafts its body bones onto the legs' skeleton") {
  const auto node = [](const std::uint32_t flags, const int bone, const int track,
                       std::vector<SkeletonNode> children = {}) {
    SkeletonNode out;
    out.flags = flags;
    out.bone = bone;
    out.track = track;
    out.children = std::move(children);
    return out;
  };
  // Legs: a torso with one body bone, then the hip and a foot.
  const SkeletonNode legs = node(
      0x1, -1, 0,
      {node(node_turns_with_aim, 0, 2, {node(0x100, 1, 3)}), node(0x1, 2, 4, {node(0x1, 3, 5)})});
  // The body's maker: two body bones (one animated) and the arm mount.
  SkeletonNode arms;
  arms.type = SkeletonNodeType::arms;
  const SkeletonNode body =
      node(0x1, -1, 0,
           {node(node_turns_with_aim, 0, 2, {node(0x40100, 1, 3), node(0x400, 2, -1), arms})});

  const auto grafted = graft_body_section(legs, body);
  REQUIRE(grafted != nullptr);
  const SkeletonNode &torso = grafted->children[0];
  CHECK(torso.track == 2); // the legs' own torso
  REQUIRE(torso.children.size() == 3);
  CHECK(torso.children[0].bone == 1);
  CHECK(torso.children[0].track == -1); // not the legs' bank's track 3
  CHECK(torso.children[1].bone == 2);
  CHECK(torso.children[2].type == SkeletonNodeType::arms);
  const SkeletonNode &hip = grafted->children[1];
  CHECK(hip.bone == 3); // after the body's three bones
  CHECK(hip.track == 4);
  CHECK(hip.children[0].bone == 4);
  CHECK(graft_body_section(legs, node(0x1, -1, 0)) == nullptr);
}

TEST_CASE("a wheel rolls about X by the wheel turn instead of the aim pitch") {
  // Flag 0x200 marks a wheel (0x0aa499), even with 0x100 set beside it.
  const SkeletonNode root = parse_skeleton_node(json::parse(R"({"type": 0, "track": -1,
    "translation": [0, 0, 0], "rotation": [0, 0, 0], "bone": 0, "flags": 768})"));
  PoseInput input;
  input.aim_pitch = 0.3F;
  input.wheel_turn = 1.5707963F;
  Pose pose;
  build_pose(root, input, pose);
  CHECK(near(transform_point({0, 1, 0}, pose[0].bones[0]), {0.0F, 0.0F, 1.0F}));
}

TEST_CASE("another maker's weapons hang from the body's maker's mounts") {
  const auto catalogue = parse_robot_catalogue(json::parse(R"({
    "parts": {"body": [0, 1], "legs": [0, 1], "arm": [0, 1, 2, 3], "shoulder": [0, 1]},
    "body_types": [{"model": 38, "motions": 1722}, {"model": 63, "motions": 1748}],
    "leg_types": [{"model": 39, "motions": 1727, "arm_motions": [[10, 11], [12, 13]]},
                  {"model": 64, "motions": 1753, "arm_motions": [[20, 21], [22, 23]]}],
    "arm_types": [{"models": [9, 0], "class": 0}, {"models": [10, 1], "class": 1},
                  {"models": [53, 44], "class": 1}, {"models": [54, 45], "class": 0}],
    "shoulder_types": [{"models": [28, 18]}, {"models": [72, 62]}],
    "body_leg_types": [0, 1], "arm_maker_types": [0, 2], "shoulder_maker_types": [0, 1],
    "shoulder_mounts": [[-1, 1, -0.8], [-0.8, 0.5, -1]]})"));
  SkeletonNode torso;
  torso.flags = node_turns_with_aim;
  torso.bone = 0;
  SkeletonNode legs;
  legs.children.push_back(torso);
  RobotSkeletons skeletons;
  skeletons.legs = {legs, legs};
  for (auto &side : skeletons.arms) {
    for (int type = 0; type < 4; ++type) {
      SkeletonNode arm;
      arm.track = type; // tells the skeletons apart
      side.emplace_back(arm);
    }
  }
  RobotParts parts;
  parts.arms = {2, 3};
  parts.shoulders = {1, 1};
  RobotAssembly robot = assemble_robot(catalogue, skeletons, parts);
  CHECK(robot.arm_skeletons[0]->track == 1); // the body's maker's class 1
  CHECK(robot.arm_skeletons[1]->track == 0); // and class 0
  CHECK(robot.arm_motions[0] == "motions/11.json");
  CHECK(near(robot.shoulder_shifts[0], {-0.2F, 0.5F, 0.2F}));
  CHECK(near(robot.shoulder_shifts[1], {0.2F, 0.5F, 0.2F}));
  parts.legs = 1; // the other maker's legs: the body's own legs' arm motions
  robot = assemble_robot(catalogue, skeletons, parts);
  CHECK(robot.grafted != nullptr);
  CHECK(robot.arm_motions[0] == "motions/11.json");
  parts.body = 1; // the arms' own maker
  robot = assemble_robot(catalogue, skeletons, parts);
  CHECK(robot.arm_skeletons[0]->track == 2);
  CHECK(robot.arm_motions[0] == "motions/21.json");
  CHECK(near(robot.shoulder_shifts[0], {0.0F, 0.0F, 0.0F}));
}
