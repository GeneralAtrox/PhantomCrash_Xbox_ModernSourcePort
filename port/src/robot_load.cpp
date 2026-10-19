// A robot's weight and the speed and jump its legs manage under it
// (0x075f93, 0x075a36, 0x07c148, 0x07c1c4, 0x07c34e).
#include "phantomcrash/robot_load.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {

// The legs' level trims up to a tenth off both factors (0x17ac38).
constexpr float level_trim = 0.1F;
// Overloaded, the factors fall with (capacity / weight) to this power.
constexpr float overload_power = 10.0F;
// The reference frame of the jump factor: body 0 and legs 0 at level 0.
constexpr int reference_part = 0;
// The dash value's range and the walk's, half of it (0x199f7c, 0x1b5ae0,
// 0x17cd04, 0x19c64c).
constexpr float dash_min = 20.0F;
constexpr float dash_max = 2000.0F;
constexpr float walk_min = 10.0F;
constexpr float walk_max = 1000.0F;
constexpr float walk_share = 0.5F;
// The jump height's range (0x19f89c, 0x17ccd4).
constexpr float jump_min = 2.5F;
constexpr float jump_max = 100.0F;
// Values over the model's motion speed give the playback rate (0x1b5ab4).
constexpr float motion_rate_scale = 1.0F / 216.0F;
// The turn's range, in degrees a battle tick (0x17ac38, 0x19ef0c).
constexpr float turn_min = 0.1F;
constexpr float turn_max = 90.0F;
// The dodge's push range, units a battle tick (0x17ac38, 0x17ccfc). 0x08d98c
// adds it times the 60 Hz frames a tick (DAT_a69c94 / DAT_ac73b8).
constexpr float dodge_min = 0.1F;
constexpr float dodge_max = 30.0F;
constexpr float frames_per_battle_tick = part_frames_per_second / battle_tick_rate;

template <typename Record>
const Record *record(const std::vector<Record> &records, const int index) {
  return index >= 0 && index < static_cast<int>(records.size())
             ? &records[static_cast<std::size_t>(index)]
             : nullptr;
}

// An older extraction leaves a curve out: every term is zero.
bool has_curve(const LevelCurve &curve) {
  return curve.base != 0.0F || curve.mid != 0.0F || curve.top != 0.0F;
}

float weight_of(const ArmourRecord *part, const int level) {
  return part != nullptr ? part->weight.value(level) : 0.0F;
}

// 0x07c148 and 0x07c1c4: the frame's share of the weight, trimmed by the
// legs' level and cut by any overload.
float factor(const float frame, const LoadStats &load, const int legs_level) {
  if (load.weight <= 0.0F) {
    return 1.0F;
  }
  const float trim = 1.0F - static_cast<float>(legs_level) / part_level_max * level_trim;
  float out = trim * std::sqrt(frame / load.weight);
  if (load.capacity < load.weight) {
    out *= std::pow(load.capacity / load.weight, overload_power);
  }
  return out;
}

} // namespace

LoadStats load_stats(const PartTables &tables, const RobotLoadout &loadout) {
  const ArmourRecord *body = record(tables.bodies, loadout.body);
  const ArmourRecord *legs = record(tables.legs, loadout.legs);
  LoadStats load;
  load.weight = weight_of(body, loadout.body_level) + weight_of(legs, loadout.legs_level);
  if (const ChipRecord *chip = tables.chip(loadout.chip)) {
    load.weight += chip->weight.value(loadout.chip_level);
  }
  for (const PartRef &part : loadout.weapons) {
    if (const WeaponRecord *weapon = tables.weapon(part)) {
      load.weight += weapon->weight.value(part.level);
    }
  }
  load.capacity = legs != nullptr ? legs->capacity.value(loadout.legs_level) : 0.0F;
  const float frame = weight_of(body, 0) + weight_of(legs, 0);
  const float reference = weight_of(record(tables.bodies, reference_part), 0) +
                          weight_of(record(tables.legs, reference_part), 0);
  load.speed_factor = factor(frame, load, loadout.legs_level);
  load.jump_factor = factor(reference, load, loadout.legs_level);
  return load;
}

LegsSpeeds legs_speeds(const PartTables &tables, const RobotLoadout &loadout,
                       const LoadStats &load) {
  const ArmourRecord *legs = record(tables.legs, loadout.legs);
  if (legs == nullptr) {
    return {};
  }
  LegsSpeeds speeds;
  speeds.dash =
      std::clamp(legs->speed.value(loadout.legs_level) * load.speed_factor, dash_min, dash_max);
  speeds.walk = std::clamp(speeds.dash * walk_share, walk_min, walk_max);
  speeds.jump =
      std::clamp(legs->jump.value(loadout.legs_level) * load.jump_factor, jump_min, jump_max);
  return speeds;
}

MovementTuning loaded_movement(const PartTables &tables, const RobotLoadout &loadout,
                               const LoadStats &load, MovementTuning base) {
  const float speed = load.speed_factor;
  const ArmourRecord *legs = record(tables.legs, loadout.legs);
  if (legs != nullptr) {
    base.wheels = legs->wheels;
    base.stop_keep = legs->stop_keep;
  }
  if (legs == nullptr || !has_curve(legs->turn)) {
    base.turn_degrees_per_second *= speed;
    base.dash_turn_degrees_per_second *= speed;
    base.air_turn_degrees_per_second *= speed;
  } else {
    // 0x07c34e: the legs' turn times the speed factor (robot +0x7c, +0x78),
    // for the ground, the dash and the air alike. 0x08e257 and 0x0905af
    // turn by it times the 60 Hz frames a tick, as 0x07d330 twists.
    const float turn =
        std::clamp(legs->turn.value(loadout.legs_level) * speed, turn_min, turn_max) *
        frames_per_battle_tick * battle_tick_rate;
    base.turn_degrees_per_second = turn;
    base.dash_turn_degrees_per_second = turn;
    base.air_turn_degrees_per_second = turn;
    base.twist_degrees_per_second = turn;
  }
  if (legs == nullptr) {
    base.jump_height *= std::max(0.0F, load.jump_factor);
    return base;
  }
  // 0x07c34e: the legs' dodge pushes times the speed factor (robot +0x80,
  // +0x84), on the ground and in the air.
  const auto push = [&](const LevelCurve &curve, const float fallback) {
    if (!has_curve(curve)) {
      return fallback;
    }
    return std::clamp(curve.value(loadout.legs_level) * speed, dodge_min, dodge_max) *
           frames_per_battle_tick * battle_tick_rate;
  };
  base.dodge_speed = push(legs->dodge, base.dodge_speed);
  base.air_dodge_speed = push(legs->air_dodge, base.air_dodge_speed);
  const LegsSpeeds speeds = legs_speeds(tables, loadout, load);
  base.jump_height =
      has_curve(legs->jump) ? speeds.jump : base.jump_height * std::max(0.0F, load.jump_factor);
  const float dash = speeds.dash;
  const float walk = speeds.walk;
  const auto *motion = record(tables.leg_motion_speeds, legs->model);
  const float walk_motion = motion != nullptr ? (*motion)[0] : 0.0F;
  const float dash_motion = motion != nullptr ? (*motion)[1] : 0.0F;
  const float walk_rate = walk_motion > 0.0F ? walk * motion_rate_scale / walk_motion : speed;
  const float dash_rate = dash_motion > 0.0F ? dash * motion_rate_scale / dash_motion : speed;
  // A motionless roller/hover dash still translates. Its clamped speed is
  // in km/h, not an animation playback rate or the unclamped load factor.
  const float walk_units = walk * motion_rate_scale * part_frames_per_second;
  const float dash_units = dash * motion_rate_scale * part_frames_per_second;
  const float back_share = base.walk_speed > 0.0F ? base.back_speed / base.walk_speed : 1.0F;
  const float dash_back_share =
      base.dash_speed > 0.0F ? base.dash_back_speed / base.dash_speed : 1.0F;
  // 0x08e6b6 / 0x08f2da move a flagged model by its motion's root, whose
  // stride the table holds a frame; without a stride of its own the robot
  // takes the table's. An unflagged model (0 in the table, as the rollers)
  // moves at its speed even when its clip's root drifts a little (+0x64,
  // +0x6c clear).
  base.walk_stride = base.walk_stride && walk_motion > 0.0F;
  base.dash_stride = base.dash_stride && dash_motion > 0.0F;
  if (walk_motion > 0.0F && !base.walk_stride) {
    base.walk_speed = walk_motion * part_frames_per_second;
    base.back_speed = base.walk_speed * back_share;
  }
  if (dash_motion > 0.0F && !base.dash_stride) {
    base.dash_speed = dash_motion * part_frames_per_second;
    base.dash_back_speed = base.dash_speed * dash_back_share;
  }
  base.walk_speed = walk_motion > 0.0F ? base.walk_speed * walk_rate : walk_units;
  base.back_speed = walk_motion > 0.0F ? base.back_speed * walk_rate : walk_units * back_share;
  base.dash_speed = dash_motion > 0.0F ? base.dash_speed * dash_rate : dash_units;
  base.dash_back_speed =
      dash_motion > 0.0F ? base.dash_back_speed * dash_rate : dash_units * dash_back_share;
  if (walk_motion > 0.0F) {
    base.walk_motion_rate = walk_rate;
  }
  if (dash_motion > 0.0F) {
    base.dash_motion_rate = dash_rate;
  }
  return base;
}

RobotLoadout hangar_loadout(const Garage &garage, const int hangar) {
  if (hangar < 0 || hangar >= static_cast<int>(garage.hangars.size())) {
    return {};
  }
  return hangar_loadout(garage, garage.hangars[static_cast<std::size_t>(hangar)]);
}

RobotLoadout hangar_loadout(const Garage &garage, const Hangar &fitted) {
  RobotLoadout loadout;
  const auto entry = [&garage, &fitted](const PartSlot slot) -> const StockItem * {
    const auto found = garage.stock.find(fitted.part(slot));
    return found != garage.stock.end() ? &found->second : nullptr;
  };
  if (const StockItem *chip = entry(PartSlot::chip)) {
    loadout.chip = chip->id;
    loadout.chip_level = chip->level;
  }
  if (const StockItem *body = entry(PartSlot::body)) {
    loadout.body = body->id;
    loadout.body_level = body->level;
  }
  if (const StockItem *legs = entry(PartSlot::legs)) {
    loadout.legs = legs->id;
    loadout.legs_level = legs->level;
  }
  for (const PartSlot slot : {PartSlot::right_arm, PartSlot::left_arm, PartSlot::right_shoulder,
                              PartSlot::left_shoulder}) {
    if (const StockItem *weapon = entry(slot)) {
      const WeaponTable table =
          weapon->kind == PartKind::arm ? WeaponTable::arm : WeaponTable::shoulder;
      loadout.weapons.push_back({table, weapon->id, weapon->level});
    }
  }
  return loadout;
}

} // namespace phantomcrash
