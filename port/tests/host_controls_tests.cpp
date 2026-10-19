#include "test_support.hpp"

#include "phantomcrash/settings.hpp"
#include "phantomcrash/training_battle.hpp"

#include <algorithm>
#include <cmath>

using namespace phantomcrash;

TEST_CASE("desktop bindings and mouse preferences survive a settings save") {
  Settings settings;
  bind_host_key(settings.host_controls, "left_trigger", "W");
  bind_host_pad(settings.host_controls, "left_trigger", "a");
  settings.host_controls.invert_mouse_y = true;
  settings.host_controls.mouse_degrees_per_pixel = 0.32F;
  settings.host_controls.stick_dead_zone = 0.2F;
  const Settings restored = settings_from_json(settings_to_json(settings));
  CHECK(restored.host_controls == settings.host_controls);
  const auto &bindings = restored.host_controls.bindings;
  const auto forward = std::ranges::find(bindings, "up", &HostBinding::action);
  CHECK(std::ranges::find(forward->keys, "W") == forward->keys.end());
  const auto jump = std::ranges::find(bindings, "a", &HostBinding::action);
  CHECK(jump->pad.empty());
  CHECK(settings_from_json(json::parse("{}")).host_controls == HostControls{});
}

TEST_CASE("left and right mouse clicks fire only their corresponding arm mounts") {
  InputFrame input;
  input.mouse_fire_left = true;
  const FireControl left = battle_fire(input, BattleButtons{});
  CHECK(left.fire[static_cast<std::size_t>(WeaponSlot::left_arm)]);
  CHECK(!left.fire[static_cast<std::size_t>(WeaponSlot::right_arm)]);
  CHECK(!left.fire[static_cast<std::size_t>(WeaponSlot::left_shoulder)]);
  CHECK(!left.fire[static_cast<std::size_t>(WeaponSlot::right_shoulder)]);
  input.mouse_fire_left = false;
  input.mouse_fire_right = true;
  const FireControl right = battle_fire(input, BattleButtons{});
  CHECK(right.fire[static_cast<std::size_t>(WeaponSlot::right_arm)]);
  CHECK(!right.fire[static_cast<std::size_t>(WeaponSlot::left_arm)]);
}

TEST_CASE("mouse aim is a displacement and does not repeat after it is consumed") {
  MovementTuning tuning;
  RobotMover mover(tuning);
  mover.place({}, 0.0F);
  InputFrame input;
  input.mouse_aim = {0.2F, 0.1F};
  mover.tick(battle_control(input, BattleButtons{}, nullptr), 1.0F / 60.0F);
  CHECK(std::abs(mover.pitch() - 0.1F) < 0.0001F);
  for (int tick = 0; tick < 30; ++tick) {
    mover.tick(RobotControl{}, 1.0F / 30.0F);
  }
  CHECK(std::abs(mover.heading() - 0.2F) < 0.0001F);
  CHECK(std::abs(mover.pitch() - 0.1F) < 0.0001F);
}

TEST_CASE("mouse and stick look reach and stop at 45 degrees in both directions") {
  for (const float direction : {-1.0F, 1.0F}) {
    RobotMover mouse(MovementTuning{});
    RobotMover stick(MovementTuning{});
    mouse.place({}, 0.0F);
    stick.place({}, 0.0F);
    RobotControl moved;
    moved.mouse_aim[1] = direction * radians(90.0F);
    mouse.tick(moved, 1.0F / 30.0F);
    RobotControl held;
    held.pitch = direction;
    for (int tick = 0; tick < 60; ++tick) {
      stick.tick(held, 1.0F / 30.0F);
    }
    CHECK(std::abs(mouse.pitch() - direction * radians(45.0F)) < 0.0001F);
    CHECK(std::abs(stick.pitch() - direction * radians(45.0F)) < 0.0001F);
    moved.mouse_aim[1] = -direction * radians(3.0F);
    held.pitch = -direction;
    mouse.tick(moved, 1.0F / 30.0F);
    stick.tick(held, 1.0F / 30.0F);
    CHECK(std::abs(mouse.pitch() - direction * radians(42.0F)) < 0.0001F);
    CHECK(std::abs(stick.pitch() - direction * radians(42.0F)) < 0.0001F);
  }
}

TEST_CASE("the mouse turns no faster than the stick, at the legs' turn rate") {
  MovementTuning tuning;
  tuning.turn_degrees_per_second = 60.0F;
  RobotMover stick(tuning);
  RobotMover mouse(tuning);
  stick.place({}, 0.0F);
  mouse.place({}, 0.0F);
  InputFrame input;
  input.right_stick = {1, 0}; // Standard: the right stick turns
  InputFrame moved;
  moved.mouse_aim = {radians(90), 0};
  for (int tick = 0; tick < 15; ++tick) {
    stick.tick(battle_control(input, BattleButtons{}, nullptr, true), 1.0F / 30.0F);
    mouse.tick(battle_control(tick == 0 ? moved : InputFrame{}, BattleButtons{}, nullptr, true),
               1.0F / 30.0F);
  }
  CHECK(std::abs(stick.heading() - radians(30)) < 1e-3F);
  CHECK(std::abs(mouse.heading() - radians(30)) < 1e-3F);
  for (int tick = 0; tick < 45; ++tick) {
    mouse.tick(RobotControl{}, 1.0F / 30.0F);
  }
  CHECK(std::abs(mouse.heading() - radians(90)) < 1e-3F); // the rest follows, and no more
}

TEST_CASE("moving the mouse back reverses the turn at once, dropping what was left") {
  MovementTuning tuning;
  tuning.turn_degrees_per_second = 60.0F;
  RobotMover mouse(tuning);
  mouse.place({}, 0.0F);
  InputFrame swipe;
  swipe.mouse_aim = {radians(90), 0};
  mouse.tick(battle_control(swipe, BattleButtons{}, nullptr, true), 1.0F / 30.0F);
  for (int tick = 0; tick < 14; ++tick) {
    mouse.tick(RobotControl{}, 1.0F / 30.0F);
  }
  const float turned = mouse.heading(); // 30 degrees, with 60 still to turn
  InputFrame back;
  back.mouse_aim = {-radians(1), 0};
  mouse.tick(battle_control(back, BattleButtons{}, nullptr, true), 1.0F / 30.0F);
  CHECK(mouse.heading() < turned);
  for (int tick = 0; tick < 30; ++tick) {
    mouse.tick(RobotControl{}, 1.0F / 30.0F);
  }
  CHECK(std::abs(mouse.heading() - (turned - radians(1))) < 1e-3F);
}

TEST_CASE("left stick and AD strafe while right stick turns the whole robot") {
  MovementTuning tuning;
  tuning.dash_hold_seconds = 10;
  for (const float side : {-1.0F, 1.0F}) {
    RobotMover mover(tuning);
    mover.place({}, 0);
    InputFrame input;
    input.left_stick = {side, 0};
    const auto control = battle_control(input, BattleButtons{}, nullptr);
    for (int frame = 0; frame < 60; ++frame) {
      mover.tick(control, 1.0F / 60);
    }
    CHECK(std::abs(mover.heading()) < 1e-5F);
    CHECK(dot(mover.position(), heading_right(0)) * side > 1);
    CHECK(std::abs(mover.position().z) < 1e-5F);
    for (int frame = 0; frame < 60; ++frame) {
      mover.tick({}, 1.0F / 60);
    }
    CHECK(length(mover.velocity()) < 1e-5F);
  }
  RobotMover turning(tuning);
  turning.place({}, 0);
  InputFrame input;
  input.right_stick = {1, 0};
  for (int frame = 0; frame < 120; ++frame) {
    turning.tick(battle_control(input, BattleButtons{}, nullptr), 1.0F / 60);
  }
  CHECK(std::abs(wrap_angle(turning.heading() - radians(220))) < 1e-4F);
  CHECK(length(turning.position()) < 1e-5F);
  CHECK(std::abs(turning.twist()) < 1e-5F);
}

TEST_CASE("diagonal movement stays at walking speed and mouse turning rotates a strafe") {
  MovementTuning tuning;
  tuning.dash_hold_seconds = 10;
  for (const int rate : {30, 60, 144}) {
    RobotMover mover(tuning);
    mover.place({}, 0);
    InputFrame input;
    input.left_stick = {1, 1};
    for (int frame = 0; frame < rate; ++frame) {
      mover.tick(battle_control(input, BattleButtons{}, nullptr), 1.0F / rate);
    }
    CHECK(std::abs(length(mover.velocity()) - tuning.walk_speed) < 1e-4F);
    mover.place({}, 0);
    input.left_stick = {1, 0};
    input.mouse_aim = {radians(90), 0};
    mover.tick(battle_control(input, BattleButtons{}, nullptr), 1.0F / rate);
    input.mouse_aim = {};
    for (int frame = 0; frame < rate; ++frame) { // the turn rate spreads it
      mover.tick(battle_control(input, BattleButtons{}, nullptr), 1.0F / rate);
    }
    CHECK(std::abs(mover.velocity().x) < 1e-5F);
    CHECK(dot(mover.velocity(), heading_right(radians(90))) > 0);
    input.mouse_aim = {};
    input.held = BattleButtons{}.jump;
    mover.tick(battle_control(input, BattleButtons{}, nullptr), 1.0F / rate);
    const float previous = dot(mover.velocity(), heading_right(mover.heading()));
    mover.tick(battle_control(input, BattleButtons{}, nullptr), 1.0F / rate);
    CHECK(!mover.grounded());
    CHECK(dot(mover.velocity(), heading_right(mover.heading())) > previous);
  }
}
