#include "test_support.hpp"
#include "phantomcrash/source_leg_playback.hpp"
#include "phantomcrash/skeleton.hpp"
#include "phantomcrash/robot_library.hpp"
#include "phantomcrash/json_fields.hpp"
#include <cmath>

using namespace phantomcrash;

namespace {
constexpr float tick = 1.0F / 60;
SkeletonNode fixture() {
  SkeletonNode root;
  root.source_model_root = true;
  root.track = root.bone = 0;
  root.flags = 1;
  SkeletonNode leg;
  leg.track = leg.bone = 1;
  leg.flags = 1;
  leg.translation = {0, 2, 0};
  root.children.push_back(leg);
  return root;
}
Motion motion(const float x, const float angle = 0) {
  Motion m;
  m.frames = 100;
  m.tracks.resize(2);
  for (std::size_t track = 0; track < 2; ++track) {
    auto &out = m.tracks[track];
    out.flags = track_translation | track_rotation;
    out.translation.emplace();
    out.rotation.emplace();
    const std::array<float, 3> at{track == 0 ? 800.0F : x, track == 0 ? 0.0F : -2.0F, 0};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      (*out.translation)[axis].values = {at[axis]};
      (*out.rotation)[axis].values = {axis == 2 && track == 1 ? angle : 0};
    }
  }
  return m;
}
void near(const float a, const float b) {
  CHECK(std::abs(a - b) < 0.0001F);
}
} // namespace

TEST_CASE("source leg counters feed back controls independently of the moving clip") {
  const auto rig = fixture();
  const auto clip = motion(8);
  SourceLegPlayback state;
  state.reset(rig);
  state.play(4);
  MotionSource source;
  source.motion = &clip;
  for (const float x : {2.0F, 5.0F, 7.25F, 8.0F}) {
    source.frame += 20; // playback speed does not advance the transition counter
    state.advance(source, tick, true);
    near(state.locals()[1]->at(3, 0), x);
    near(state.locals()[0]->at(3, 0), 0); // root-key travel belongs to the actor
  }
  CHECK(state.transition_frame() == 4);
  CHECK(state.transition_frames() == 4);
  state.advance(source, tick, true);
  CHECK(state.transition_frames() == 0);
  state.play(2);
  const auto reverse = motion(-4);
  source.motion = &reverse;
  state.advance(source, tick, true);
  near(state.locals()[1]->at(3, 0), 2);
  state.play(4);
  source.motion = &clip;
  state.advance(source, tick, true);
  near(state.locals()[1]->at(3, 0), 3.5F); // interrupted evaluated pose retained
}

TEST_CASE("source wheel callbacks and repeated attachment queries preserve leg channel poses") {
  auto rig = fixture();
  rig.children[0].flags = node_wheel;
  const auto clip = motion(8);
  SourceLegPlayback state;
  state.reset(rig);
  state.play(4);
  MotionSource source;
  source.motion = &clip;
  source.frame = 1;
  state.advance(source, tick, true);
  PoseInput input;
  input.root = translation({10, 0, 0});
  input.source_leg_locals = &state.locals();
  input.wheel_turn = .5F;
  for (int query = 0; query < 3; ++query) {
    Pose pose;
    build_pose(rig, input, pose);
    near(pose[0].bones[1].at(3, 0), 12);
    near(pose[0].bones[1].at(3, 1), 2);
    near(pose[0].bones[1].at(1, 2), std::sin(.5F));
    near(state.locals()[1]->at(1, 2), 0);
    CHECK(state.transition_frame() == 1);
    CHECK(!source_leg_transform(rig.children[0], input, 1)); // native arm remains independent
  }
}

TEST_CASE("unmapped source leg states keep the existing native transition adapter") {
  auto rig = fixture();
  rig.children[0].flags = node_wheel;
  const auto old = motion(-2, -.3F);
  const auto current = motion(8, .7F);
  MotionSource source;
  source.motion = &current;
  source.frame = 10;
  source.previous_count = 1;
  source.previous[0] = {&old, 2};
  source.weight = .25F;
  SourceLegPlayback state;
  state.reset(rig);
  state.advance(source, tick, false);
  PoseInput input;
  input.legs = source;
  input.wheel_turn = .6F;
  Pose legacy, cached;
  build_pose(rig, input, legacy);
  input.source_leg_locals = &state.locals();
  build_pose(rig, input, cached);
  for (std::size_t bone = 0; bone < 2; ++bone) {
    for (std::size_t element = 0; element < 16; ++element) {
      near(legacy[0].bones[bone].m[element], cached[0].bones[bone].m[element]);
    }
  }
}

TEST_CASE(
    "every imported leg player keeps the mixed torso on its evaluated control during transitions") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto file = roots.find_asset("slai/native_parts.json");
  if (!file) {
    return;
  }
  const auto parts = json::read_file(*file);
  RobotLibrary library(roots);
  REQUIRE(library.available());
  int body = -1;
  for (const auto &part : json::required(parts, "parts").array()) {
    if (part.string_or("model", "") == "S4B01H") {
      body = static_cast<int>(part.number_or("part", -1));
    }
  }
  REQUIRE(body >= 0);
  int checked = 0;
  for (const auto &part : json::required(parts, "parts").array()) {
    if (part.string_or("category", "") != "leg") {
      continue;
    }
    RobotParts selection;
    selection.body = body;
    selection.legs = static_cast<int>(part.number_or("part", -1));
    const auto assembly = assemble_robot(library.catalogue(), library.skeletons(), selection);
    const auto *bank = library.bank(assembly.leg_motions);
    REQUIRE(bank != nullptr);
    RobotAnimator animator(library, assembly);
    for (const int action : {28, 0, 1, 2, 5, 8, 9, 10, 11, 12}) {
      const int before = animator.motion();
      animator.play(action, true, 6, 0, true);
      if (!bank->motion(action)) {
        CHECK(animator.motion() == before); // disabled source setter keeps the previous action
      } else {
        CHECK(animator.motion() == action);
      }
      animator.set_speed(action % 2 == 0 ? 2.5F : .25F);
      for (int update = 0; update < 12; ++update) {
        animator.tick(tick);
        Pose pose, again;
        const Mat4 root = rotation_y(.7F) * translation({17, 3, -21});
        animator.pose(root, 0, 0, true, pose);
        animator.pose(root, 0, 0, true, again);
        REQUIRE(pose[0].source_model_root);
        REQUIRE(pose[0].source_controls.size() > 1);
        REQUIRE(pose[0].source_controls[1]);
        for (std::size_t element = 0; element < 16; ++element) {
          near(pose[0].bones[0].m[element], pose[0].source_controls[1]->m[element]);
          near(pose[0].bones[0].m[element], again[0].bones[0].m[element]);
          CHECK(std::isfinite(pose[0].bones[0].m[element]));
        }
        near(pose[0].source_model_root->at(3, 0), 17);
        near(pose[0].source_model_root->at(3, 1), 3);
        near(pose[0].source_model_root->at(3, 2), -21);
      }
    }
    ++checked;
  }
  CHECK(checked == 35);
}
