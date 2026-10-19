#include "test_support.hpp"
#include "phantomcrash/motion.hpp"
#include "phantomcrash/robot.hpp"
#include "phantomcrash/robot_mover.hpp"
#include "phantomcrash/training_battle.hpp"
#include "phantomcrash/host_controls.hpp"
#include "phantomcrash/robot_combat.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

using namespace phantomcrash;

namespace {
Motion stride() {
  Motion motion;
  motion.frames = 8;
  MotionChannel zero;
  zero.values = {0};
  MotionChannel forward;
  forward.kind = ChannelKind::linear;
  forward.keys = {2, 4, 6, 8};
  forward.values = {0, -1, -4, -6, -7};
  MotionTrack track;
  track.translation = std::array{zero, zero, forward};
  motion.tracks.push_back(track);
  return motion;
}
bool near(const float a, const float b) {
  return std::abs(a - b) < 1e-4F;
}

struct AnimatedStride {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("phantomcrash-stride-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  DataRoots roots{path, path / "mods", {path}};
  std::unique_ptr<RobotLibrary> library;
  SkeletonNode skeleton;
  RobotAssembly assembly;

  AnimatedStride() {
    std::filesystem::create_directories(path / "world");
    std::ofstream(path / "world/robot_animation.json") << "{}";
    const std::string zero = R"({"kind":"constant","values":[0]})";
    const std::string travel = R"({"kind":"linear","keys":[2,4,6,8],"values":[0,-1,-4,-6,-7]})";
    std::string bank = R"({"track_count":2,"motions":[)";
    for (int i = 0; i < 29; ++i) {
      if (i != 0) {
        bank += ',';
      }
      bank += R"({"frames":8,"tracks":[{}, {"translation":[)" + zero + ',' + zero + ',' +
              (i == 0 ? travel : zero) + "]}]}";
    }
    std::ofstream(path / "leg.json") << bank << "]}";
    library = std::make_unique<RobotLibrary>(roots);
    skeleton.bone = 0;
    SkeletonNode root;
    root.flags = node_root_motion;
    root.track = 1;
    root.bone = 1;
    skeleton.children.push_back(root);
    assembly.skeleton = &skeleton;
    assembly.leg_motions = "leg.json";
    assembly.timings.resize(29, MotionTiming{0, 2, 6, 8});
  }
  ~AnimatedStride() {
    std::filesystem::remove_all(path);
  }
};
} // namespace

TEST_CASE("battle movement and the simulation pose use the same extracted travel track and phase") {
  AnimatedStride fixture;
  BattleSettings settings;
  settings.movement.walk_stride = true;
  settings.movement.dash_hold_seconds = 100;
  settings.movement.stop_keep = 0.5F;
  Battle battle(settings, EnemyPath{});
  battle.add_robot("player", 0, {}, 0, {}, 100);
  auto pose = std::make_shared<RobotCombat>(*fixture.library, fixture.assembly);
  battle.robot(0).pose = pose;
  RobotControl input;
  input.move[1] = 1;
  constexpr float tick = 1.0F / 30;
  const float positions[] = {-1, -4, -6, -9};
  const float frames[] = {2, 4, 6, 4};
  for (int i = 0; i < 4; ++i) {
    battle.tick(input, {}, tick);
    CHECK(near(battle.player().mover.position().z, positions[i]));
    CHECK(near(pose->motion_frame(), frames[i]));
  }
  battle.tick({}, {}, tick);
  CHECK(pose->motion() == robot_motion::stand);
  CHECK(near(battle.player().mover.position().z, -10.5F));
  battle.tick({}, {}, tick);
  CHECK(pose->motion() == robot_motion::brake);
  CHECK(near(battle.player().mover.position().z, -11.25F));
}

TEST_CASE("stick axes reproduce the original dead zone and rescale both signs symmetrically") {
  CHECK(host_stick_axis(16384) == 0);
  CHECK(host_stick_axis(-16384) == 0);
  CHECK(host_stick_axis(32767) == 1);
  CHECK(host_stick_axis(-32768) == -1);
  CHECK(near(host_stick_axis(24576), 8192.0F / 16383.0F));
  CHECK(near(host_stick_axis(-24576), -8192.0F / 16383.0F));
  CHECK(host_stick_axis(10000) == 0);
  CHECK(host_stick_axis(10000, 0.2F) > 0);
}

TEST_CASE("root travel follows the stride phase and joins loop boundaries without a jump") {
  const Motion motion = stride();
  const MotionTiming timing{0, 2, 6, 8};
  CHECK(near(*root_motion_step(motion, timing, 0, 2, true), 1));
  CHECK(near(*root_motion_step(motion, timing, 2, 2, true), 3));
  CHECK(near(*root_motion_step(motion, timing, 5, 3, true), 4));
  CHECK(near(*root_motion_step(motion, timing, 5, 11, true), 14));
  CHECK(near(*root_motion_step(motion, timing, 6, 8, false), 1));
}

TEST_CASE("loaded forward movement uses root displacement and the leg's stopping momentum") {
  MotionBank bank;
  bank.motions.resize(29);
  bank.motions[robot_motion::walk] = stride();
  std::vector<MotionTiming> timings(29, MotionTiming{0, 2, 6, 8});
  MovementTuning tuning;
  tuning.walk_stride = true;
  tuning.dash_hold_seconds = 100;
  tuning.walk_accel = 0.01F; // Must not replace the root's first step with a slow ramp.
  tuning.stop_keep = 0.5F;
  RobotMover mover(tuning);
  mover.set_timings(&timings);
  RootMotionSource source{&bank};
  RobotControl input;
  input.move[1] = 1;
  constexpr float tick = 1.0F / 30;
  mover.tick(input, tick, source);
  CHECK(near(mover.position().z, -1));
  source = {&bank, mover.motion(), 2, mover.motion_revision()};
  mover.tick(input, tick, source);
  CHECK(near(mover.position().z, -4));
  source.frame = 4;
  mover.tick({}, tick, source);
  CHECK(mover.mode() == MoveMode::stand);
  CHECK(near(mover.position().z, -5.5F)); // Previous 90 units/s, halved before displacement.
  CHECK(near(length(mover.velocity()), 45));
  source = {&bank, mover.motion(), 2, mover.motion_revision()};
  mover.tick({}, tick, source);
  CHECK(mover.mode() == MoveMode::brake);
  CHECK(near(mover.position().z, -6.25F));
  CHECK(near(length(mover.velocity()), 22.5F));
  for (int i = 0; i < 5; ++i) {
    mover.tick({}, tick, source);
  }
  CHECK(near(length(mover.velocity()), 0));
}

TEST_CASE("air strafing retains independent yaw and original airborne turning is selectable") {
  for (const bool strafe : {false, true}) {
    MovementTuning tuning;
    tuning.standard_air_strafe = strafe;
    RobotMover mover(tuning);
    InputFrame input;
    input.left_stick[0] = 1;
    input.held = BattleButtons{}.jump;
    mover.tick(battle_control(input, BattleButtons{}, nullptr), 1.0F / 30);
    const float before = mover.local_velocity().x;
    mover.tick(battle_control(input, BattleButtons{}, nullptr), 1.0F / 30);
    CHECK(!mover.grounded());
    CHECK(strafe ? near(mover.heading(), 0) : mover.heading() > 0);
    CHECK(strafe ? mover.local_velocity().x > before : near(mover.local_velocity().x, before));
  }
}

TEST_CASE("a double flick dodges to the same side as the corresponding shoulder button") {
  for (const int side : {-1, 1}) {
    RobotMover flicked(MovementTuning{}), pressed(MovementTuning{});
    RobotControl input;
    input.standard = true;
    input.strafe = static_cast<float>(side);
    flicked.tick(input, 1.0F / 30);
    input.strafe = 0;
    flicked.tick(input, 1.0F / 30);
    input.strafe = static_cast<float>(side);
    flicked.tick(input, 1.0F / 30);
    input = {};
    input.dodge = side;
    pressed.tick(input, 1.0F / 30);
    CHECK(flicked.dodged() == side);
    CHECK(flicked.motion() == pressed.motion());
    CHECK(flicked.motion() == (side > 0 ? 13 : 14)); // 0x0774ad -> 0x08d98c.
    flicked.tick({}, 1.0F / 30);                     // The next dodge-state tick applies the push.
    CHECK(flicked.local_velocity().x * side > 0);
  }
}
