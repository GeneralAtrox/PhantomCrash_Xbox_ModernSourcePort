#include "test_support.hpp"
#include "phantomcrash/rigid_animation.hpp"
#include "phantomcrash/world_model.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/robot.hpp"
#include <cmath>

using namespace phantomcrash;

namespace {
constexpr float tick = 1.0F / 60;
void near(const float a, const float b) {
  CHECK(std::abs(a - b) < .0001F);
}
SourcePmtTrack track(const float first, const float second, const float duration) {
  SourcePmtTrack out;
  SourcePmtKey a, b;
  a.duration = duration;
  a.position = Vec3{first, 0, 0};
  b.duration = 99; // interpolation endpoint, not another segment
  b.position = Vec3{second, 0, 0};
  out.keys = {a, b};
  return out;
}
RigidRig fixture() {
  RigidRig rig;
  rig.nodes.resize(3);
  rig.nodes[0].parent = 2;
  rig.nodes[1].parent = 0;
  rig.nodes[2].position = {-10, 0, 0};
  rig.nodes[2].local = translation({-10, 0, 0});
  rig.motion_clips = {"walk", "attack", ""};
  rig.independent_motions = {1};
  rig.preserve_disabled_clips = true;
  rig.weapon_clip = "attack";
  RigidClip walk, attack;
  walk.name = "walk";
  walk.source_tracks = {track(10, 18, 4), track(0, 6, 3)};
  attack.name = "attack";
  attack.source_tracks = {track(10, 22, 6), track(0, 6, 3)};
  rig.clips = {walk, attack};
  return rig;
}
} // namespace

TEST_CASE("rigid PMT controls own their cursors and preserve the native mount hierarchy") {
  const auto rig = fixture();
  RigidSourcePlayback state;
  state.start(rig.clips[0], 0, true);
  for (int frame = 0; frame < 13; ++frame) {
    const auto pose = state.sample(rig);
    near(pose[0].position[0], 10 + (frame % 4) * 2.0F);
    near(pose[1].position[0], (frame % 3) * 2.0F);
    CHECK(pose[0].parent == 2);
    CHECK(pose[1].parent == 0);
    CHECK(pose[2].local.m == rig.nodes[2].local.m);
    const auto again = state.sample(rig);
    CHECK(again[0].local.m == pose[0].local.m);
    state.advance(1, true);
  }
}

TEST_CASE("body PMT clocks evaluate before advancing and retain accepted disabled playback") {
  const auto rig = fixture();
  RigidPlayback state;
  state.update(rig, 0, 500, true, tick, 0, -1, 2);
  near(state.nodes[0].position[0], 10); // native global frame does not sample source controls
  state.update(rig, 0, 500, true, tick, 0, -1, 0);
  near(state.nodes[0].position[0], 14);
  state.update(rig, 0, 500, true, tick, 0, -1, 0);
  near(state.nodes[0].position[0], 14);
  state.update(rig, 1, 900, false, tick, 1, -1, 0);
  near(state.nodes[0].position[0], 10); // independent attack starts its own clock
  state.update(rig, 2, 900, true, tick, 2, -1, 10);
  CHECK(state.motion == 1);
  CHECK(!state.loop);
  near(state.nodes[0].position[0], 12); // rejected action retains the attack's rate
}

TEST_CASE("weapon PMT playback restarts on a shot and holds the source nonloop segment") {
  const auto rig = fixture();
  RigidPlayback state;
  state.update_weapon(rig, tick, false, false);
  CHECK(state.nodes[0].local.m == rig.nodes[0].local.m);
  for (int frame = 0; frame < 10; ++frame) {
    state.update_weapon(rig, tick, true, frame == 0);
    near(state.nodes[0].position[0], 10 + std::min(frame, 5) * 2.0F);
    near(state.nodes[1].position[0], std::min(frame, 2) * 2.0F);
  }
  state.update_weapon(rig, tick, true, true);
  near(state.nodes[0].position[0], 10);
  state.update_weapon(rig, tick, true, false);
  near(state.nodes[0].position[0], 12);
}

TEST_CASE("PMT rotation keys preserve authored weapon bind scale through transitions") {
  auto rig = fixture();
  auto &control = rig.clips[0].source_tracks[1];
  control.bind = scaling({.91F, .91F, .91F});
  for (auto &key : control.keys) {
    key.rotation = std::array<float, 4>{0, 0, 0, 1};
  }
  rig.nodes[1].local = control.bind;
  rig.nodes[1].scale = {.91F, .91F, .91F};
  rig.transition_frames = {4};
  RigidPlayback state;
  for (int update = 0; update < 6; ++update) {
    state.update(rig, 0, 50, true, tick);
    for (std::size_t axis = 0; axis < 3; ++axis) {
      near(state.nodes[1].scale[axis], .91F);
      const auto &m = state.nodes[1].local;
      near(length({m.at(static_cast<int>(axis), 0), m.at(static_cast<int>(axis), 1),
                   m.at(static_cast<int>(axis), 2)}),
           .91F);
    }
  }
}

TEST_CASE("every registered alternate body bank loads its original PMT control payloads") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto file = roots.find_asset("slai/native_parts.json");
  if (!file) {
    return;
  }
  const auto parts = json::read_file(*file);
  const auto catalogue =
      parse_robot_catalogue(json::read_file(roots.asset("robots/catalogue.json")));
  int models = 0;
  for (const auto &part : json::required(parts, "parts").array()) {
    if (part.string_or("category", "") != "body") {
      continue;
    }
    const auto &body =
        catalogue.body_types.at(static_cast<std::size_t>(part.number_or("type", -1)));
    for (const auto &[leg, resource] : body.models_by_leg_type) {
      const auto path = "models/binlink/" + std::to_string(resource) + ".gltf";
      const auto model = load_model(roots.asset(path), path);
      REQUIRE(model.rig != nullptr);
      for (const auto &clip : model.rig->clips) {
        REQUIRE(!clip.source_tracks.empty());
        CHECK(clip.source_tracks.size() <= model.rig->nodes.size());
        RigidSourcePlayback state;
        state.start(clip, 0, true);
        const auto pose = state.sample(*model.rig);
        for (const auto &node : pose) {
          for (const float value : node.local.m) {
            CHECK(std::isfinite(value));
          }
        }
      }
      ++models;
    }
  }
  CHECK(models == 35);
}
