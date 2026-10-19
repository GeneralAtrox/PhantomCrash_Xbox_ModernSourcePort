#include "test_support.hpp"
#include "phantomcrash/rigid_animation.hpp"
#include "phantomcrash/world_model.hpp"
#include <cmath>
#include <memory>

using namespace phantomcrash;

namespace {
RigidRig fixture() {
  RigidRig rig;
  rig.nodes.resize(2);
  rig.nodes[1].parent = 0;
  rig.nodes[1].position = {0, 2, 0};
  rig.nodes[1].local = translation({0, 2, 0});
  rig.arm_mount_nodes = {1, 1};
  rig.shoulder_mount_nodes = {0, 0};
  rig.preserve_disabled_clips = true;
  rig.motion_clips = {"idle", "forward", "back", ""};
  rig.transition_frames = {0, 4, 2, 0};
  for (const auto &[name, x] : {std::pair{"idle", 0.0F}, {"forward", 8.0F}, {"back", -4.0F}}) {
    RigidClip clip;
    clip.name = name;
    clip.duration = 1;
    RigidChannel channel;
    channel.node = 0;
    channel.path = RigidChannel::Path::position;
    channel.times = {0, 1};
    channel.values = {{x, 0, 0, 0}, {x, 0, 0, 0}};
    clip.channels.push_back(channel);
    rig.clips.push_back(clip);
  }
  return rig;
}
constexpr float tick = 1.0F / 60;
void near(const float actual, const float expected) {
  CHECK(std::abs(actual - expected) < 0.0001F);
}
} // namespace

TEST_CASE("source secondary transitions feed back poses and promote on the following update") {
  auto rig = fixture();
  RigidPlayback state;
  state.update(rig, 0, 0, true, tick);
  state.update(rig, 1, 40, true, tick);
  CHECK(state.primary_clip == "idle");
  CHECK(state.clip == "forward");
  near(state.nodes[0].position[0], 2);
  // A huge change in the clip frame does not accelerate the transition.
  state.update(rig, 1, 400, true, tick);
  CHECK(state.transition_frame == 2);
  near(state.nodes[0].position[0], 5); // feedback: 2 + (8-2) * 2/4
  state.update(rig, 1, 400, true, tick);
  near(state.nodes[0].position[0], 7.25F);
  state.update(rig, 1, 400, true, tick);
  near(state.nodes[0].position[0], 8);
  CHECK(state.primary_clip == "idle");
  state.update(rig, 1, 400, true, tick);
  CHECK(state.primary_clip == "forward");
  CHECK(state.transition_frames == 0);
}

TEST_CASE("source interrupted transitions and disabled actions retain evaluated controls") {
  auto rig = fixture();
  RigidPlayback state;
  state.update(rig, 0, 0, true, tick);
  state.update(rig, 1, 1, true, tick);
  near(state.nodes[0].position[0], 2);
  state.update(rig, 2, 1, false, tick);
  near(state.nodes[0].position[0], -1); // interrupted pose -> new target at 1/2
  CHECK(state.primary_clip == "idle");
  state.update(rig, 3, 900, true, tick);
  CHECK(state.motion == 2);
  CHECK(!state.loop);
  near(state.nodes[0].position[0], -4);
  state.update(rig, 3, 900, true, tick);
  CHECK(state.primary_clip == "back");
  CHECK(state.transition_frames == 0);
}

TEST_CASE("source turn transitions accept the assembled leg and actor-state duration") {
  auto rig = fixture();
  for (const int duration : {1, 8, 10}) {
    RigidPlayback state;
    state.update(rig, 0, 0, true, tick);
    state.update(rig, 1, 1, true, tick, 1, duration);
    CHECK(state.transition_frames == duration);
    near(state.nodes[0].position[0], 8.0F / duration);
    // A later selector value must not restart an already accepted transition.
    state.update(rig, 1, 2, true, tick, 1, 100);
    CHECK(state.transition_frames == (duration == 1 ? 0 : duration));
  }
}

TEST_CASE("source draws hands shoulders and effect attachments share one transition pose") {
  auto rig = std::make_shared<RigidRig>(fixture());
  RigidPlayback state;
  state.update(*rig, 0, 0, true, tick);
  state.update(*rig, 1, 1, true, tick);
  ModelData model;
  model.rig = rig;
  ModelNode drawing;
  drawing.rig_node = 1;
  model.nodes.push_back(drawing);
  for (int read = 0; read < 3; ++read) {
    std::vector<Mat4> draws;
    std::array<Mat4, 2> arms, shoulders;
    Mat4 effect;
    pose_rigid_model(model, state.clip, 900, false, translation({10, 0, 0}), draws, &arms, 1,
                     &effect, &shoulders, &state.nodes);
    near(draws[0].at(3, 0), 12);
    near(draws[0].at(3, 1), 2);
    CHECK(draws[0].m == arms[0].m);
    CHECK(draws[0].m == effect.m);
    near(shoulders[0].at(3, 0), 12);
    CHECK(state.transition_frame == 1);
  }
}

TEST_CASE("source quaternion transitions take the short arc and retain control scale") {
  auto rig = fixture();
  RigidChannel rotation;
  rotation.node = 0;
  rotation.path = RigidChannel::Path::rotation;
  rotation.times = {0};
  const float half = std::sqrt(0.5F);
  rotation.values = {{0, -half, 0, -half}}; // same 90 degree rotation with negated quaternion
  rig.clips[1].channels.push_back(rotation);
  RigidChannel scale = rotation;
  scale.path = RigidChannel::Path::scale;
  scale.values = {{3, 5, 1, 0}};
  rig.clips[1].channels.push_back(scale);
  RigidPlayback state;
  state.update(rig, 0, 0, true, tick);
  state.update(rig, 1, 0, true, tick);
  near(state.nodes[0].scale[0], 1.5F);
  near(state.nodes[0].scale[1], 2);
  near(std::abs(state.nodes[0].rotation[1]), std::sin(3.14159265359F / 16));
  near(std::abs(state.nodes[0].rotation[3]), std::cos(3.14159265359F / 16));
}
