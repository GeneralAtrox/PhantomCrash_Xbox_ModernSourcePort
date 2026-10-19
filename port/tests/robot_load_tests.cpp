// A robot's load (0x075f93, 0x075a36): its weight, the speed and jump
// factors, and the walk and dash rates its legs manage under it.

#include "test_support.hpp"

#include <algorithm>
#include <cmath>

#include "phantomcrash/robot_load.hpp"

using namespace phantomcrash;

namespace {

bool close_to(const float a, const float b) {
  return std::fabs(a - b) < 1e-3F;
}

LevelCurve flat(const float value) {
  LevelCurve curve;
  curve.base = value;
  return curve;
}

// The training player's parts: body 0 (395), legs 0 (280, carrying 1060,
// speed 118, model 0), Dog I (25.1) and four weapons (40, 90, 80, 55).
PartTables test_tables() {
  PartTables tables;
  ArmourRecord body;
  body.weight = flat(395.0F);
  tables.bodies.push_back(body);
  ArmourRecord legs;
  legs.weight = flat(280.0F);
  legs.capacity = flat(1060.0F);
  legs.speed = flat(118.0F);
  legs.model = 0;
  tables.legs.push_back(legs);
  legs.model = 1; // a model whose dash has no root motion
  tables.legs.push_back(legs);
  tables.leg_motion_speeds = {{1.0F / 3.0F, 0.533333F}, {0.266667F, 0.0F}};
  ChipRecord chip;
  chip.weight = flat(25.1F);
  tables.chips.push_back(chip);
  for (const float weight : {40.0F, 90.0F}) {
    WeaponRecord weapon;
    weapon.index = static_cast<int>(tables.arm_weapons.size());
    weapon.weight = flat(weight);
    tables.arm_weapons.push_back(weapon);
  }
  for (const float weight : {80.0F, 55.0F}) {
    WeaponRecord weapon;
    weapon.index = static_cast<int>(tables.shoulder_weapons.size());
    weapon.weight = flat(weight);
    tables.shoulder_weapons.push_back(weapon);
  }
  return tables;
}

RobotLoadout test_loadout() {
  RobotLoadout loadout;
  loadout.chip = 0;
  loadout.body = 0;
  loadout.legs = 0;
  loadout.weapons = {{WeaponTable::arm, 0, 0},
                     {WeaponTable::arm, 1, 0},
                     {WeaponTable::shoulder, 0, 0},
                     {WeaponTable::shoulder, 1, 0}};
  return loadout;
}

} // namespace

TEST_CASE("a robot's weight slows it by the square root of its frame's share") {
  const PartTables tables = test_tables();
  const LoadStats load = load_stats(tables, test_loadout());
  CHECK(close_to(load.weight, 965.1F));
  CHECK(close_to(load.capacity, 1060.0F));
  CHECK(close_to(load.speed_factor, std::sqrt(675.0F / 965.1F)));
  CHECK(close_to(load.jump_factor, load.speed_factor)); // body 0 and legs 0
  RobotLoadout bare;
  CHECK(load_stats(tables, bare).speed_factor == 1.0F); // weighs nothing
}

TEST_CASE("an overloaded robot slows sharply and the legs' level trims the speed") {
  PartTables tables = test_tables();
  const float light = load_stats(tables, test_loadout()).speed_factor;
  RobotLoadout trained = test_loadout();
  trained.legs_level = part_level_max;
  CHECK(close_to(load_stats(tables, trained).speed_factor, light * 0.9F));
  tables.legs[0].capacity = flat(482.55F); // half the weight
  CHECK(close_to(load_stats(tables, test_loadout()).speed_factor, light * std::pow(0.5F, 10.0F)));
}

TEST_CASE("the load sets the leg motions' rates and the speeds with them") {
  PartTables tables = test_tables();
  tables.legs[0].stop_keep = 0.94F;
  const RobotLoadout loadout = test_loadout();
  const LoadStats load = load_stats(tables, loadout);
  MovementTuning strides;
  strides.walk_speed = 18.0F;
  strides.dash_speed = 30.0F;
  strides.walk_stride = true;
  strides.dash_stride = true;
  strides.turn_degrees_per_second = 100.0F;
  const MovementTuning moved = loaded_movement(tables, loadout, load, strides);
  CHECK(close_to(moved.stop_keep, 0.94F)); // Part retention is independent of load.
  // The dash: 118 x 0.8363 / 216 / 0.5333; the walk: half that value / 216 / 0.3333.
  const float dash_rate = 118.0F * load.speed_factor / 216.0F / 0.533333F;
  const float walk_rate = 59.0F * load.speed_factor / 216.0F * 3.0F;
  CHECK(close_to(moved.dash_motion_rate, dash_rate));
  CHECK(close_to(moved.dash_speed, 30.0F * dash_rate));
  CHECK(close_to(moved.walk_motion_rate, walk_rate));
  CHECK(close_to(moved.walk_speed, 18.0F * walk_rate));
  // Without strides of their own the motions take the table's, a frame.
  const MovementTuning tabled = loaded_movement(tables, loadout, load, MovementTuning{});
  CHECK(close_to(tabled.walk_speed, 59.0F * load.speed_factor / 216.0F * 60.0F));
  CHECK(close_to(tabled.dash_speed, 118.0F * load.speed_factor / 216.0F * 60.0F));
  CHECK(close_to(tabled.walk_motion_rate, walk_rate));
  // These legs have no turn or jump curves: the speed and jump factors scale `strides`.
  CHECK(close_to(moved.turn_degrees_per_second, 100.0F * load.speed_factor));
  CHECK(close_to(moved.jump_height, strides.jump_height * load.jump_factor));

  RobotLoadout roller = loadout;
  roller.legs = 1; // no root motion: translate at the clamped legs speed
  const MovementTuning rolled = loaded_movement(tables, roller, load, strides);
  CHECK(close_to(rolled.dash_speed, legs_speeds(tables, roller, load).dash / 3.6F));
  CHECK(rolled.dash_motion_rate == 1.0F);
  // A measured root drift does not make an unflagged motion move the robot:
  // the rollers' clips wobble a few thousandths a second and would stall it.
  CHECK(rolled.walk_stride);
  CHECK(!rolled.dash_stride);
  LoadStats overloaded = load;
  overloaded.speed_factor = 0.000001F;
  const MovementTuning slow = loaded_movement(tables, roller, overloaded, strides);
  CHECK(close_to(slow.dash_speed, legs_speeds(tables, roller, overloaded).dash / 3.6F));
  RobotMover mover(slow);
  mover.place({}, 0.0F);
  RobotControl forward;
  forward.move[1] = 1.0F;
  for (int frame = 0; frame < 120; ++frame) {
    mover.tick(forward, 1.0F / 60.0F);
  }
  const Vec3 after_dash = mover.position();
  for (int frame = 0; frame < 60; ++frame) {
    mover.tick(forward, 1.0F / 60.0F);
  }
  CHECK(length(mover.position() - after_dash) > 5.0F);
}

TEST_CASE("the legs' turn and jump set the robot's own under the load") {
  PartTables tables = test_tables();
  tables.legs[0].turn = flat(2.35F);
  tables.legs[0].jump = flat(19.14F);
  const RobotLoadout loadout = test_loadout();
  const LoadStats load = load_stats(tables, loadout);
  const MovementTuning moved = loaded_movement(tables, loadout, load, MovementTuning{});
  // Degrees a 60 Hz frame, two frames a battle tick, on the ground, in a
  // dash and in the air (0x08e257, 0x0905af).
  const float turn = 2.35F * load.speed_factor * 2.0F * battle_tick_rate;
  CHECK(close_to(moved.turn_degrees_per_second, turn));
  CHECK(close_to(moved.dash_turn_degrees_per_second, turn));
  CHECK(close_to(moved.air_turn_degrees_per_second, turn));
  CHECK(close_to(moved.jump_height, legs_speeds(tables, loadout, load).jump));
}

TEST_CASE("a held jump rises its height and a tap the throw after the take-off") {
  MovementTuning tuning;
  tuning.jump_height = 19.14F;
  const auto peak = [&](const int held_ticks) {
    RobotMover mover(tuning);
    mover.place({}, 0.0F);
    float top = 0.0F;
    for (int tick = 0; tick < 300; ++tick) {
      RobotControl control;
      control.jump = tick < held_ticks;
      mover.tick(control, 1.0F / battle_tick_rate);
      top = std::max(top, mover.position().y);
    }
    return top;
  };
  const float held = peak(300);
  const float tap = peak(1);
  CHECK(held > 0.95F * tuning.jump_height);
  CHECK(held < 1.1F * tuning.jump_height);
  CHECK(tap < 0.3F * tuning.jump_height);
  CHECK(tap > 0.1F * tuning.jump_height);
}
