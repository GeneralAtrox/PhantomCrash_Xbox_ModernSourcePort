// The finale's epilogue (robot states 8-18 in battle mode 3: 0x08ac52,
// 0x08b017, 0x08b397) and the data that names it.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/finale.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace phantomcrash;

namespace {

constexpr double frame = 1.0 / 60.0;

MotionChannel constant(const float value) {
  MotionChannel channel;
  channel.values = {value};
  return channel;
}

SceneTrack track(const float frames) {
  SceneTrack scene;
  scene.frames = frames;
  scene.camera.fov = constant(50.0F);
  scene.camera.eye = {constant(1.0F), constant(2.0F), constant(3.0F)};
  scene.camera.target = {constant(4.0F), constant(5.0F), constant(6.0F)};
  return scene;
}

FinalePhase phase(const int state, const float wait, const float fade_in, const float fade_out) {
  FinalePhase row;
  row.state = state;
  row.wait_frames = wait;
  row.fade_in_frames = fade_in;
  row.fade_out_frames = fade_out;
  return row;
}

// Steps `frames` frames, keeping every cue.
std::vector<FinaleCue> run(FinaleSequence &finale, const int frames) {
  std::vector<FinaleCue> cues;
  for (int step = 0; step < frames; ++step) {
    finale.update(frame);
    for (FinaleCue &cue : finale.take_cues()) {
      cues.push_back(std::move(cue));
    }
  }
  return cues;
}

int count(const std::vector<FinaleCue> &cues, const std::string &name) {
  return static_cast<int>(std::ranges::count(cues, name, &FinaleCue::name));
}

} // namespace

TEST_CASE("a finale phase waits frozen, fades in, plays its scene and fades out with frames left") {
  FinaleTables tables;
  tables.phases = {phase(8, 45.0F, 5.0F, 60.0F), phase(9, 60.0F, 60.0F, 0.0F)};
  FinaleTuning tuning;
  FinaleSequence finale(tables, tuning, {{8, track(135.0F)}, {9, track(210.0F)}});
  run(finale, 3);
  CHECK(finale.fade() == 1.0F); // the start's two-frame fade-out
  CHECK(finale.frozen());
  const auto waiting = run(finale, 41);
  CHECK(count(waiting, "finale_start") == 1); // as the wait passes frame 15
  CHECK(finale.stage() == FinaleStage::wait);
  run(finale, 1);
  CHECK(finale.stage() == FinaleStage::scene);
  CHECK(!finale.frozen());
  run(finale, 3);
  CHECK(finale.fade() > 0.0F);
  CHECK(finale.fade() < 1.0F);
  run(finale, 3);
  CHECK(finale.fade() == 0.0F);
  // 135 frames: the fade-out starts with 60 left and the scene ends with 2.
  run(finale, 135 - 6 - 60);
  CHECK(finale.fade() == 0.0F);
  run(finale, 30);
  CHECK(finale.fade() > 0.4F);
  run(finale, 28);
  CHECK(finale.stage() == FinaleStage::leave);
  run(finale, 1);
  CHECK(finale.phase()->state == 9);
  CHECK(finale.stage() == FinaleStage::wait);
  // State 9's scene starts the ending music.
  const auto music = run(finale, 61);
  CHECK(std::ranges::any_of(music, [](const FinaleCue &cue) {
    return cue.kind == FinaleCue::Kind::music && cue.name == "ending_theme";
  }));
}

TEST_CASE("a finale line shows at its frame with the chatter sound, but not the last state's") {
  FinaleTables tables;
  tables.scripts = {json::JsonValue{json::JsonValue::Array{}},
                    json::JsonValue{json::JsonValue::Array{}}};
  FinalePhase talk = phase(10, 0.0F, 0.0F, 0.0F);
  talk.script = 0;
  talk.script_frame = 30.0F;
  FinalePhase show = phase(18, 0.0F, 0.0F, 0.0F);
  show.script = 1;
  show.script_frame = 30.0F;
  tables.phases = {talk, show};
  FinaleSequence finale(tables, FinaleTuning{}, {{10, track(150.0F)}, {18, track(450.0F)}});
  const auto first = run(finale, 40);
  const auto line = std::ranges::find(first, FinaleCue::Kind::script, &FinaleCue::kind);
  REQUIRE(line != first.end());
  CHECK(line->script == 0);
  CHECK(count(first, "chatter") == 1);
  const auto rest = run(finale, 150);
  CHECK(std::ranges::count(rest, FinaleCue::Kind::script, &FinaleCue::kind) == 1);
}

TEST_CASE("the call-in show rings at its frames, blinks its banners, then the TV goes off") {
  FinaleTables tables;
  tables.phases = {phase(18, 0.0F, 0.0F, 0.0F)};
  tables.call_ins = {31, 73};
  FinaleTuning tuning;
  FinaleSequence finale(tables, tuning, {{18, track(450.0F)}});
  const auto early = run(finale, 74); // the wait's frame, then 73 of the scene
  CHECK(count(early, "chatter") == 2);
  // Once the callers are out they ring every other frame.
  const auto busy = run(finale, 10);
  CHECK(count(busy, "chatter") == 5);
  run(finale, 450 - 84 - 60);
  bool shown = false;
  bool hidden = false;
  for (int step = 0; step < 20; ++step) {
    run(finale, 1);
    shown = shown || finale.banner();
    hidden = hidden || !finale.banner();
  }
  CHECK(shown);
  CHECK(hidden);
  const auto end = run(finale, 60);
  CHECK(std::ranges::any_of(end, [](const FinaleCue &cue) { return cue.name == "tv_off"; }));
  CHECK(!finale.done());
  run(finale, static_cast<int>(tuning.end_frames));
  CHECK(finale.done());
  CHECK(finale.fade() == 1.0F);
}

TEST_CASE("the finale's cameras and walks are in world coordinates, the z channels negated") {
  SceneTrack scene = track(100.0F);
  scene.walk = std::array<MotionChannel, 3>{constant(104.0F), constant(3.0F), constant(17.0F)};
  const CameraShot shot = finale_shot(scene, 10.0F, Gate{});
  CHECK(shot.eye.x == 1.0F);
  CHECK(shot.eye.z == -3.0F);
  CHECK(shot.target.z == -6.0F);
  CHECK(shot.fov_degrees == 50.0F);
  const Vec3 walk = finale_walk(scene, 10.0F, Gate{});
  CHECK(walk.x == 104.0F);
  CHECK(walk.y == 0.0F); // the floor puts it down
  CHECK(walk.z == 17.0F);
}

TEST_CASE("the shipped finale names its stage, its ending and the original's timing") {
  const auto roots = DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT);
  const FinaleTuning tuning = parse_finale_tuning(roots.document("story/finale.json"));
  CHECK(tuning.stage == "umihotaru_day");
  CHECK(tuning.next == "ending");
  CHECK(tuning.music_state == 9);
  CHECK(tuning.banner_frames == 60.0F);
  CHECK(tuning.end_frames == 120.0F);
  CHECK(tuning.end_music == "tv_off");
}
