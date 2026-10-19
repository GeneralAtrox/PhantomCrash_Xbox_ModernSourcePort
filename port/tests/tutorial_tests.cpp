// Pepper's in-battle lessons: the courses' steps, their messages and goals.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/training_ground.hpp"
#include "phantomcrash/tutorial.hpp"

using namespace phantomcrash;

namespace {

std::vector<LessonStep> course(const char *text) {
  return parse_lesson_courses(json::parse(text)).at(0);
}

LessonSample walking(const float speed) {
  LessonSample sample;
  sample.mode = MoveMode::walk;
  sample.local_velocity = {0.0F, 0.0F, speed};
  sample.seconds = 1.0F;
  return sample;
}

// The stick and buttons a player would use for `step`'s goal, `tick` ticks
// into the practice.
InputFrame practice_input(const LessonStep &step, const int tick, const BattleButtons &buttons) {
  InputFrame input;
  const bool first_half = tick % 120 < 60;
  switch (step.goal) {
  case LessonGoal::walk_both_ways:
    input.left_stick = {0.0F, first_half ? 1.0F : -1.0F};
    break;
  case LessonGoal::turn_right:
  case LessonGoal::turn_left:
    input.right_stick = {step.goal == LessonGoal::turn_right ? 1.0F : -1.0F, 0.0F};
    break;
  case LessonGoal::dash:
    input.left_stick = {0.0F, 1.0F};
    input.pressed = tick == 0 ? buttons.dash : 0;
    break;
  case LessonGoal::jump:
  case LessonGoal::high_jump:
    input.held = tick % 120 < 40 ? buttons.jump : 0;
    break;
  case LessonGoal::dodge_both:
    input.pressed = tick % 30 == 0 ? (first_half ? buttons.dodge_left : buttons.dodge_right) : 0;
    break;
  case LessonGoal::twist_both:
    // Two seconds each way: the twist turns 90 degrees a second.
    input.right_stick = {tick % 240 < 120 ? 1.0F : -1.0F, 0.0F};
    break;
  case LessonGoal::twist_reset:
    input.pressed = buttons.reset_twist;
    break;
  case LessonGoal::camouflage:
    input.pressed = buttons.camouflage;
    break;
  default:
    break;
  }
  return input;
}

} // namespace

TEST_CASE("the shipped basic and camouflage courses can be finished with their controls") {
  const auto roots = DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT);
  const TrainingScript script = parse_training(roots.document("world/training.json"));
  const TrainingBattle &setup = script.battle;
  for (const int mode : {0, 2}) {
    Battle battle(setup.settings, EnemyPath{});
    battle.add_robot("player", 0, setup.player_start, setup.player_heading, setup.player.weapons,
                     setup.player.hp);
    battle.add_robot("target", 1, {0.0F, 0.0F, -40.0F}, 0.0F, {}, 100.0F);
    Tutorial tutorial(setup.courses.at(mode));
    constexpr float tick = 1.0F / 60.0F;
    int practice_ticks = 0;
    for (int frame = 0; frame < 60 * 600 && !tutorial.finished(); ++frame) {
      if (tutorial.message()) {
        tutorial.message_done();
        practice_ticks = 0;
        continue;
      }
      const LessonStep &step = *tutorial.step();
      const InputFrame input = practice_input(step, practice_ticks++, setup.buttons);
      if (tutorial.take_place_drone()) {
        battle.place_facing_player(1, step.place_drone);
      }
      const float heading = battle.player().mover.heading();
      battle.tick(battle_control(input, setup.buttons, &step), battle_fire(input, setup.buttons),
                  tick);
      tutorial.update(lesson_sample(battle.player(), heading, tick));
    }
    CHECK(tutorial.finished());
  }
}

TEST_CASE("the shipped attack course can be finished with each weapon") {
  const auto roots = DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT);
  const TrainingScript script = parse_training(roots.document("world/training.json"));
  const TrainingBattle &setup = script.battle;
  // Without a path the drone respawns at the middle, straight ahead.
  Battle battle(setup.settings, EnemyPath{});
  battle.add_robot("player", 0, setup.player_start, setup.player_heading, setup.player.weapons,
                   setup.player.hp);
  battle.add_drone("drone", setup.drone.hp);
  Tutorial tutorial(setup.courses.at(1));
  constexpr float tick = 1.0F / 60.0F;
  for (int frame = 0; frame < 60 * 600 && !tutorial.finished(); ++frame) {
    if (tutorial.message()) {
      tutorial.message_done();
      continue;
    }
    const LessonStep &step = *tutorial.step();
    battle.set_drones_active(step.drones);
    battle.set_allowed_weapons(step.weapons);
    const Combatant &drone = battle.robots()[1];
    const float distance = length(drone.mover.position() - battle.player().mover.position());
    // Close in for the drill; keep some range for the rest, as missiles
    // need room to turn.
    const bool melee = step.weapons == (1U << static_cast<unsigned>(WeaponSlot::left_arm));
    const float wanted = melee ? 5.0F : 30.0F;
    InputFrame input;
    if (drone.alive && distance > wanted + 2.0F) {
      input.left_stick = {0.0F, 0.6F};
    } else if (drone.alive && distance < wanted - 5.0F) {
      input.left_stick = {0.0F, -0.6F};
    }
    for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
      if ((step.weapons & (1U << slot)) != 0 && frame % 2 == 0) {
        input.held |= setup.buttons.fire[slot];
      }
    }
    const float heading = battle.player().mover.heading();
    battle.tick(battle_control(input, setup.buttons, &step), battle_fire(input, setup.buttons),
                tick);
    tutorial.update(lesson_sample(battle.player(), heading, tick));
  }
  CHECK(tutorial.finished());
  CHECK(battle.player().kills >= 22);
}

TEST_CASE("lesson courses parse goals, weapons and limits") {
  const auto steps = course(R"({"0": [
    {"state": 25, "goal": "walk_both_ways", "amount": 15, "walk_limit": 0.7},
    {"state": 36, "goal": "kills", "amount": 3, "drones": true, "weapons": ["right_arm", "left_shoulder"]},
    {"state": 34, "end": true}
  ]})");
  CHECK(steps.size() == 3);
  CHECK(steps[0].goal == LessonGoal::walk_both_ways);
  CHECK(steps[0].walk_limit == 0.7F);
  CHECK(steps[1].weapons == 0b1001U);
  CHECK(steps[1].drones);
  CHECK(steps[2].end);
  CHECK_THROWS(parse_lesson_courses(json::parse(R"({"0": [{"goal": "fly"}]})")));
  CHECK_THROWS(parse_lesson_courses(json::parse(R"({"0": [{"weapons": ["tail"]}]})")));
}

TEST_CASE("a lesson step talks, waits for its goal, then talks again") {
  Tutorial tutorial(course(R"({"0": [
    {"state": 24},
    {"state": 25, "goal": "walk_both_ways", "amount": 15},
    {"state": 34, "end": true}
  ]})"));
  CHECK(tutorial.message() == std::pair(24, 0));
  tutorial.message_done();
  CHECK(tutorial.message() == std::pair(24, 1)); // a talking step has no goal
  tutorial.message_done();
  CHECK(tutorial.message() == std::pair(25, 0));
  tutorial.message_done();
  CHECK(tutorial.practising());
  CHECK(!tutorial.message());

  tutorial.update(walking(20.0F));
  CHECK(tutorial.practising()); // only forward so far
  CHECK(tutorial.progress() > 0.4F);
  tutorial.update(walking(-20.0F));
  CHECK(tutorial.message() == std::pair(25, 1));
  tutorial.message_done();
  CHECK(tutorial.message() == std::pair(34, 0));
  tutorial.message_done();
  tutorial.message_done();
  CHECK(tutorial.finished());
}

TEST_CASE("jump goals need a landing, high jumps a height") {
  Tutorial tutorial(course(R"({"0": [{"state": 30, "goal": "high_jump", "amount": 8}]})"));
  tutorial.message_done();
  LessonSample air;
  air.grounded = false;
  air.height = 5.0F;
  tutorial.update(air);
  tutorial.update(LessonSample{});
  CHECK(tutorial.practising()); // too low
  air.height = 9.0F;
  tutorial.update(air);
  tutorial.update(LessonSample{});
  CHECK(!tutorial.practising());
}

TEST_CASE("kill goals count from the step's start and place drones on request") {
  Tutorial tutorial(course(R"({"0": [
    {"state": 36, "goal": "kills", "amount": 3},
    {"state": 43, "goal": "camouflage", "place_drone": 30}
  ]})"));
  LessonSample sample;
  sample.kills = 2;
  tutorial.update(sample); // kills before the practice do not count
  tutorial.message_done();
  sample.kills = 4;
  tutorial.update(sample);
  CHECK(tutorial.kills() == 2);
  CHECK(tutorial.practising());
  sample.kills = 5;
  tutorial.update(sample);
  CHECK(!tutorial.practising());
  tutorial.message_done();
  tutorial.message_done();
  CHECK(tutorial.take_place_drone());
  CHECK(!tutorial.take_place_drone());
  sample.camouflaged = true;
  tutorial.update(sample);
  CHECK(tutorial.message() == std::pair(43, 1));
}

TEST_CASE("BUTTON CONFIG's assignments move the battle's buttons; REVERSE flips the look") {
  BattleButtons buttons;
  ControlOptions controls;
  apply_button_config(buttons, controls);
  CHECK(buttons.fire[0] == button::right_trigger); // the original's assignment
  // Swap the right arm (R) and jump (A).
  controls.bindings[0] = 0;
  controls.bindings[6] = 5;
  apply_button_config(buttons, controls);
  CHECK(buttons.fire[0] == button::a);
  CHECK(buttons.jump == button::right_trigger);
  CHECK(buttons.fire_labels[0] == "A");
  CHECK(buttons.dodge_right == button::black);
  InputFrame input;
  input.right_stick = {0.25F, 0.5F};
  CHECK(view_changed(input, controls).right_stick[1] == 0.5F);
  controls.view = 1;
  CHECK(view_changed(input, controls).right_stick[1] == -0.5F);
  CHECK(view_changed(input, controls).right_stick[0] == 0.25F);
}
