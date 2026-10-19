// The player's robot's entrance (robot states 1-4) and exit (state 6): the
// walk out of a gate, the camera and its ease into the chase view, READY
// and GO!, and the walk back into a gate that ends the battle.

#include "test_support.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>

#include "phantomcrash/battle_chatter.hpp"
#include "phantomcrash/battle_scene.hpp"
#include "phantomcrash/json.hpp"

using namespace phantomcrash;

namespace {

constexpr float tick = 1.0F / 30.0F;
const Vec3 gate_point{0.0F, 0.0F, 50.0F};

bool close_to(const Vec3 a, const Vec3 b, const float within) {
  return length(a - b) < within;
}

MotionChannel constant(const float value) {
  MotionChannel channel;
  channel.values = {value};
  return channel;
}

// Every entrance: the camera 10 units ahead of the gate and 5 up, looking
// back at it, while the robot walks 15 units forward between frames 119
// and 359 (as the game's walk does). The exit has no walk.
BattleSceneTables test_tables() {
  SceneTrack entrance;
  entrance.frames = 540.0F;
  entrance.relative = true;
  entrance.camera.roll = constant(0.0F);
  entrance.camera.fov = constant(40.0F);
  entrance.camera.eye = {constant(0.0F), constant(5.0F), constant(10.0F)}; // z stored negated
  entrance.camera.target = {constant(0.0F), constant(2.0F), constant(0.0F)};
  MotionChannel forward;
  forward.kind = ChannelKind::linear;
  forward.keys = {119.0F, 359.0F};
  forward.values = {0.0F, 0.0F, -15.0F};
  entrance.walk = std::array<MotionChannel, 3>{constant(0.0F), constant(0.0F), forward};
  BattleSceneTables tables;
  for (int state = scene_state::entrance_first; state <= scene_state::entrance_last; ++state) {
    tables.states[state] = entrance;
  }
  SceneTrack exit = entrance;
  exit.frames = 240.0F;
  exit.walk.reset();
  tables.states[scene_state::exit] = exit;
  tables.gates[SceneTuning{}.stage] = {{gate_point, 0.0F}, {{0.0F, 0.0F, -50.0F}, pi}};
  return tables;
}

BattleSettings test_settings() {
  return parse_battle_settings(json::parse(R"({"arena": {"half_size": 100}})"));
}

// A battle whose robot 0 stands at the arena's centre.
struct TestBattle {
  Battle battle{test_settings(), EnemyPath{}};
  TestBattle() {
    battle.add_robot("player", 0, {}, 0.0F, {}, 100.0F);
  }
};

bool told(const Battle &battle, const int event) {
  return std::ranges::any_of(battle.events(), [event](const BattleEvent &e) {
    return e.robot == 0 && e.kind == BattleEventKind::chatter && e.chatter == event;
  });
}

void step(BattleScene &scene, Battle &battle) {
  scene.prepare(battle, tick);
  battle.tick({}, {}, tick);
  scene.observe(battle);
}

} // namespace

TEST_CASE("the entrance walks the robot out of its gate and hands the battle over") {
  SceneTuning tuning;
  tuning.regular_battle = true;
  BattleScene scene(test_tables(), tuning, 7);
  TestBattle test;
  Battle &battle = test.battle;
  CHECK(scene.start_entrance(battle));
  CHECK(scene.state() >= scene_state::entrance_first);
  CHECK(scene.state() <= scene_state::entrance_last);
  CHECK(close_to(battle.player().mover.position(), gate_point, 0.01F));
  CHECK(scene.letterbox() == 1.0F);
  CameraShot chase;
  chase.eye = {0.0F, 5.0F, 100.0F};
  const auto opening = scene.shot(chase);
  CHECK(opening.has_value());
  if (opening) {
    CHECK(close_to(opening->eye, {0.0F, 5.0F, 40.0F}, 0.01F)); // 10 ahead of the gate
    CHECK(opening->fov_degrees == 40.0F);
  }
  step(scene, battle);
  CHECK(told(battle, chatter_event::entrance));
  std::vector<std::string> sounds;
  std::vector<SceneCue> cues;
  for (int index = 1; index < 265; ++index) { // 530 frames: 10 left
    step(scene, battle);
    std::ranges::move(scene.take_sounds(), std::back_inserter(sounds));
    if (cues.empty() || cues.back() != scene.cue()) {
      cues.push_back(scene.cue());
    }
  }
  CHECK(scene.active());
  const auto easing = scene.shot(chase);
  CHECK(easing.has_value() && close_to(easing->eye, {0.0F, 5.0F, 70.0F}, 0.5F)); // halfway
  for (int index = 0; index < 10 && scene.active(); ++index) {
    step(scene, battle);
  }
  CHECK(!scene.active());
  CHECK(!scene.shot(chase).has_value());
  CHECK(close_to(battle.player().mover.position(), {0.0F, 0.0F, 35.0F}, 0.5F));
  CHECK((sounds == std::vector<std::string>{"battle_ready", "battle_go"}));
  CHECK((cues == std::vector<SceneCue>{SceneCue::none, SceneCue::ready, SceneCue::go}));
  for (int index = 0; index < 20; ++index) {
    step(scene, battle);
  }
  CHECK(scene.letterbox() == 0.0F);
  CHECK(!scene.finished());
}

TEST_CASE(
    "the player stays shielded through entry and for the configured time after control returns") {
  BattleSettings settings = test_settings();
  settings.entry_shield.seconds = 2.0F;
  Battle battle(settings, EnemyPath{});
  battle.add_robot("player", 0, {}, 0.0F, {}, 100.0F);
  BattleScene scene(test_tables(), SceneTuning{}, 1);
  CHECK(scene.start_entrance(battle));
  CHECK(battle.player().shield == settings.entry_shield.seconds);
  int steps = 0;
  while (scene.active() && steps++ < 280) {
    step(scene, battle);
    CHECK(battle.player().shield > settings.entry_shield.seconds - 2.0F * tick);
    CHECK(battle.player().shield_glow > 0.0F);
  }
  CHECK(!scene.active());
  CHECK(steps > 260); // The entrance outlasts a shield raised only once at spawn.
  for (int index = 0; index < 45; ++index) {
    step(scene, battle);
  }
  CHECK(battle.player().shield > 0.0F);
  CHECK(battle.player().shield_glow > 0.0F);
  for (int index = 0; index < 20; ++index) {
    step(scene, battle);
  }
  CHECK(battle.player().shield == 0.0F);
  CHECK(battle.player().shield_glow == 0.0F);
}

TEST_CASE("walking back into a gate turns the robot to it, walks it in and ends the battle") {
  BattleScene scene(test_tables(), SceneTuning{}, 1);
  TestBattle test;
  Battle &battle = test.battle;
  step(scene, battle); // out of every gate
  CHECK(!scene.active());
  battle.place(0, {0.0F, 0.0F, 48.0F}, 0.0F);
  step(scene, battle);
  CHECK(scene.state() == scene_state::exit);
  CHECK(told(battle, chatter_event::left)); // whole armour: the hit-and-run line
  for (int index = 0; index < 200 && !scene.finished(); ++index) {
    step(scene, battle);
  }
  CHECK(scene.finished());
  CHECK(std::fabs(wrap_angle(battle.player().mover.heading() - pi)) < 0.11F);
  CHECK(close_to(battle.player().mover.position(), gate_point, 0.3F));
}

TEST_CASE("no exit starts while the robot stays in its first gate or the exit is off") {
  SceneTuning tuning;
  tuning.exit = false;
  BattleScene off(test_tables(), tuning, 1);
  TestBattle test;
  Battle &battle = test.battle;
  step(off, battle);
  battle.place(0, {0.0F, 0.0F, 48.0F}, 0.0F);
  step(off, battle);
  CHECK(!off.active());

  BattleScene scene(test_tables(), SceneTuning{}, 1);
  TestBattle waiting_test;
  Battle &waiting = waiting_test.battle;
  waiting.place(0, {0.0F, 0.0F, 48.0F}, 0.0F); // it never left
  step(scene, waiting);
  step(scene, waiting);
  CHECK(!scene.active());
}
