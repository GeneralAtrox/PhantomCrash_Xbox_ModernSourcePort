#include "phantomcrash/robot_parts.hpp"
#include "robot_armour.hpp"

#include "phantomcrash/battle_weapons.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/weapon_reach.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
namespace {

constexpr float value_ceiling = 1.0e8F; // 0x1c3e50: a curve's value is clamped to 0..this
constexpr float percent = 100.0F;

int integer(const json::JsonValue &value, const char *key) {
  return static_cast<int>(json::required(value, key).number());
}

LevelCurve curve(const json::JsonValue &value, const char *key) {
  return parse_level_curve(json::required(value, key));
}

// Fields older extractions lack read as zero.
LevelCurve optional_curve(const json::JsonValue &value, const char *key) {
  const json::JsonValue *entry = value.find(key);
  return entry != nullptr ? parse_level_curve(*entry) : LevelCurve{};
}

int optional_int(const json::JsonValue &value, const char *key, const int fallback = 0) {
  return static_cast<int>(value.number_or(key, fallback));
}

float optional_float(const json::JsonValue &value, const char *key) {
  return static_cast<float>(value.number_or(key, 0.0));
}

// A name the extraction may give as null (an empty table entry).
std::string optional_string(const json::JsonValue &value, const char *key) {
  const json::JsonValue *entry = value.find(key);
  return entry != nullptr && entry->is_string() ? entry->string() : std::string{};
}

ChipRecord chip_record(const json::JsonValue &value) {
  ChipRecord out;
  out.index = integer(value, "index");
  out.name = value.string_or("name", "");
  out.inner_shape = integer(value, "inner_shape");
  out.inner_value = curve(value, "inner_value");
  out.track = integer(value, "track");
  out.outer_shape = integer(value, "outer_shape");
  out.outer_value = curve(value, "outer_value");
  out.lock_frames = integer(value, "lock_frames");
  out.chatter = static_cast<int>(value.number_or("chatter", -1));
  if (const json::JsonValue *face = value.find("face"); face != nullptr && !face->is_null()) {
    out.face = parse_sprite(*face);
  }
  out.critical_percent = optional_curve(value, "critical_percent");
  out.dodge_percent = optional_curve(value, "dodge_percent");
  out.knockdown_resistance = optional_curve(value, "knockdown_resistance");
  out.weight = optional_curve(value, "weight");
  out.price = optional_curve(value, "price");
  return out;
}

WeaponRecord weapon_record(const json::JsonValue &value) {
  WeaponRecord out;
  out.index = integer(value, "index");
  out.name = value.string_or("name", "");
  out.kind = integer(value, "kind");
  out.source_melee_kind = optional_int(value, "source_melee_kind", -1);
  out.flight = optional_int(value, "flight");
  out.aim_mode = optional_int(value, "aim_mode", -1);
  out.maker = optional_int(value, "family");
  out.model = optional_int(value, "model", -1);
  out.fire_sound = optional_int(value, "fire_sound");
  out.look = optional_int(value, "effect", -1);
  out.surface_hit = optional_int(value, "surface_hit");
  out.robot_hit = optional_int(value, "robot_hit");
  out.impact = optional_int(value, "impact", -1);
  out.launch_degrees = optional_int(value, "launch_degrees");
  out.fire_frames = optional_int(value, "fire_frames");
  out.life_frames = optional_int(value, "life_frames");
  out.start_speed = optional_float(value, "start_speed");
  out.top_speed = optional_float(value, "top_speed");
  out.acceleration = optional_float(value, "acceleration");
  out.spread_degrees = optional_int(value, "spread_degrees");
  out.homing_min_degrees = optional_float(value, "homing_min_degrees");
  out.homing_degrees = curve(value, "homing_degrees");
  out.rounds_min = optional_int(value, "rounds_min", 1);
  out.rounds = optional_curve(value, "rounds");
  out.damage_min = optional_int(value, "damage_min");
  out.damage = optional_curve(value, "damage");
  out.blast_radius = optional_curve(value, "blast_radius");
  out.blast_start_radius = optional_int(value, "blast_start_radius");
  out.blast_frames = optional_curve(value, "blast_frames");
  out.blast_damage = optional_curve(value, "blast_damage");
  out.ammo_min = optional_int(value, "ammo_min");
  out.ammo = curve(value, "ammo");
  out.recharge_frames = optional_curve(value, "recharge_frames");
  out.stun = optional_curve(value, "stun");
  out.knockback = optional_curve(value, "knockback");
  out.knockdown = optional_curve(value, "knockdown");
  out.price = optional_curve(value, "price");
  out.weight = optional_curve(value, "weight");
  return out;
}

PilotRecord pilot_record(const json::JsonValue &value) {
  PilotRecord out;
  out.index = integer(value, "index");
  out.name = optional_string(value, "name");
  out.tag = optional_string(value, "tag");
  out.robot_name = optional_string(value, "robot_name");
  for (const auto &[key, into] : {std::pair{"face", &out.face}, {"robot_face", &out.robot_face}}) {
    if (const json::JsonValue *face = value.find(key);
        face != nullptr && face->is_object() && face->find("image") != nullptr) {
      *into = parse_sprite(*face);
    }
  }
  out.team = optional_string(value, "team");
  out.bounty = optional_curve(value, "bounty");
  return out;
}

template <typename Record, typename Parse>
std::vector<Record> records(const json::JsonValue &document, const char *key, Parse parse) {
  std::vector<Record> out;
  const json::JsonValue *list = document.find(key);
  if (list == nullptr) {
    return out; // an older extraction
  }
  for (const json::JsonValue &entry : list->array()) {
    try {
      out.push_back(parse(entry));
    } catch (const std::exception &error) {
      throw std::runtime_error(std::string(key) + " " + std::to_string(out.size()) + ": " +
                               error.what());
    }
  }
  return out;
}

// The game truncates integer values (ftol); some have a floor.
int truncated(const LevelCurve &curve, const int level, const int floor = 0) {
  return std::max(static_cast<int>(curve.value(level)), floor);
}

float armour_of(const std::vector<ArmourRecord> &list, const int index, const int level,
                const bool player) {
  if (index < 0 || index >= static_cast<int>(list.size())) {
    return 0.0F;
  }
  const ArmourRecord &part = list[static_cast<std::size_t>(index)];
  return std::max(part.armour.value(level), player ? part.player_floor : part.computer_floor);
}

template <typename Record> const Record *at(const std::vector<Record> &list, const int index) {
  return index >= 0 && index < static_cast<int>(list.size())
             ? &list[static_cast<std::size_t>(index)]
             : nullptr;
}

} // namespace

float LevelCurve::value(const int level) const {
  const auto amount = static_cast<float>(std::abs(level));
  const float scale = level < 0 ? static_cast<float>(low_scale) : 1.0F;
  const auto max = static_cast<float>(part_level_max);
  float out = 0.0F;
  if (knee == 0) {
    out = base + amount / max * top * scale;
  } else if (knee == part_level_max) {
    out = base + amount / max * mid * scale;
  } else if (amount < static_cast<float>(knee)) {
    out = base + amount / static_cast<float>(knee) * mid * scale;
  } else {
    const float past = (amount - static_cast<float>(knee)) / (max - static_cast<float>(knee));
    out = base + (mid + past * (top - mid)) * scale;
  }
  return std::clamp(out, 0.0F, value_ceiling);
}

const ChipRecord *PartTables::chip(const int index) const {
  return at(chips, index);
}

const WeaponRecord *PartTables::weapon(const PartRef &part) const {
  return at(part.table == WeaponTable::arm ? arm_weapons : shoulder_weapons, part.index);
}

const PilotRecord *PartTables::pilot(const int index) const {
  return at(pilots, index);
}

float robot_armour(const PartTables &tables, const int body, const int body_level, const int legs,
                   const int legs_level, const bool player) {
  return armour_of(tables.bodies, body, body_level, player) +
         armour_of(tables.legs, legs, legs_level, player);
}

CombatStats combat_stats(const PartTables &tables, const int chip, const int chip_level,
                         const int legs, const int legs_level) {
  CombatStats out;
  if (const ChipRecord *record = tables.chip(chip)) {
    out.critical_percent = record->critical_percent.value(chip_level);
    // 0x075d4a clamps the dodge to 0..100 and makes it a share.
    out.dodge_chance = std::clamp(record->dodge_percent.value(chip_level), 0.0F, percent) / percent;
    out.knockdown_resistance = record->knockdown_resistance.value(chip_level);
  }
  if (const ArmourRecord *record = at(tables.legs, legs)) {
    out.knockback_resistance = record->knockback_resistance.value(legs_level);
  }
  return out;
}

PartTables parse_part_tables(const json::JsonValue &document) {
  PartTables out;
  out.chips = records<ChipRecord>(document, "chips", chip_record);
  out.arm_weapons = records<WeaponRecord>(document, "arm_weapons", weapon_record);
  out.shoulder_weapons = records<WeaponRecord>(document, "shoulder_weapons", weapon_record);
  out.bodies = records<ArmourRecord>(document, "bodies", parse_armour_record);
  out.legs = records<ArmourRecord>(document, "legs", parse_armour_record);
  out.pilots = records<PilotRecord>(document, "pilots", pilot_record);
  if (const json::JsonValue *speeds = document.find("leg_motion_speeds")) {
    for (const json::JsonValue &model : speeds->array()) {
      out.leg_motion_speeds.push_back({static_cast<float>(model.number_or("walk", 0.0)),
                                       static_cast<float>(model.number_or("dash", 0.0))});
    }
  }
  return out;
}

PartRef parse_part_ref(const json::JsonValue &value) {
  PartRef out;
  const std::string table = value.string_or("table", "arm");
  if (table == "shoulder") {
    out.table = WeaponTable::shoulder;
  } else if (table != "arm") {
    throw std::runtime_error("unknown part table \"" + table + "\"");
  }
  out.index = integer(value, "index");
  out.level =
      std::clamp(static_cast<int>(value.number_or("level", 0.0)), -part_level_max, part_level_max);
  return out;
}

void apply_weapon_stats(WeaponTuning &weapon, const WeaponStats &stats) {
  constexpr float fps = part_frames_per_second;
  weapon.hud_kind = stats.hud_kind;
  weapon.model = stats.model;
  weapon.fire_sound = stats.fire_sound;
  weapon.flight = stats.flight;
  weapon.aim_mode = stats.aim_mode;
  weapon.look = stats.look;
  weapon.surface_hit = stats.surface_hit;
  weapon.robot_hit = stats.robot_hit;
  weapon.impact = stats.impact;
  weapon.max_ammo = stats.ammo;
  weapon.homing = stats.homing_degrees > 0.0F;
  weapon.turn_degrees_per_second = stats.homing_degrees * fps;
  weapon.damage = static_cast<float>(stats.damage);
  weapon.rounds = std::max(stats.rounds, 1);
  weapon.spread_degrees = stats.spread_degrees;
  if (stats.hud_kind == hud_kind::beam || stats.hud_kind == hud_kind::melee) {
    if (stats.fire_frames > 0.0F) {
      weapon.cooldown = stats.fire_frames / fps;
    }
  } else {
    // 0x09b4f7 fires on a battle tick (30 Hz) whose count is out and counts
    // down two frames a tick only on ticks it does not fire: a round every
    // 1 + ceil(fire_frames / 2) ticks (a fire_frames 2 gun fires 15 a second).
    const float ticks = 1.0F + std::ceil(std::max(stats.fire_frames, 0.0F) / 2.0F);
    weapon.cooldown = ticks / battle_ticks_per_second;
  }
  if (stats.life_frames > 0.0F) {
    weapon.life_seconds = stats.life_frames / fps;
  }
  if (stats.top_speed > 0.0F) {
    weapon.speed = stats.top_speed * fps;
  }
  weapon.start_speed = stats.start_speed * fps;
  weapon.acceleration = stats.acceleration * fps * fps;
  const bool shoulder = weapon.part && weapon.part->table == WeaponTable::shoulder;
  const WeaponReach reach = weapon_reach(stats, shoulder);
  weapon.range = reach.range;
  weapon.min_range = reach.min_range;
  weapon.lock_range = reach.lock_range;
  weapon.hold_degrees = reach.hold_degrees;
  if (stats.hud_kind == hud_kind::beam) {
    // 0x09b9fe: the tip runs out at the acceleration a frame, to the top speed's length.
    weapon.speed = stats.acceleration * fps;
    weapon.start_speed = weapon.acceleration = 0.0F;
  }
  weapon.launch_degrees = stats.flight == weapon_flight_lobbed ? stats.launch_degrees : 0.0F;
  weapon.blast_radius = stats.blast_radius;
  weapon.blast_start_radius = stats.blast_start_radius;
  weapon.blast_seconds = stats.blast_frames / fps;
  weapon.blast_damage = stats.blast_damage;
  // 0x097785 adds max / recharge frames once a battle tick (30 Hz), so a
  // beam fills in its recharge frames of ticks, as the shop shows it.
  weapon.recharge_seconds = stats.recharge_frames / battle_ticks_per_second;
  weapon.knockback = stats.knockback * battle_ticks_per_second;
  weapon.knockdown_percent = stats.knockdown;
  // Only a weapon with stun makes its target flinch (0x07675e, 0x08dcd1);
  // the flinch lasts the stun, at most its hit motion (MovementTuning::hit_seconds).
  weapon.stagger_seconds = stats.stun > 0.0F ? stats.stun / fps : 0.0F;
}

ChipStats chip_stats(const ChipRecord &chip, const int level) {
  ChipStats out;
  out.inner_shape = chip.inner_shape;
  out.inner_value = chip.inner_value.value(level);
  out.outer_shape = chip.outer_shape;
  out.outer_value = chip.outer_value.value(level);
  out.track = static_cast<float>(chip.track);
  out.lock_frames = static_cast<float>(chip.lock_frames);
  return out;
}

WeaponStats weapon_stats(const WeaponRecord &record, const int level) {
  WeaponStats out;
  out.hud_kind = record.kind;
  out.flight = record.flight;
  out.aim_mode = record.aim_mode;
  out.model = record.model;
  out.fire_sound = record.fire_sound;
  out.look = record.look;
  out.surface_hit = record.surface_hit;
  out.robot_hit = record.robot_hit;
  out.impact = record.impact;
  out.launch_degrees = static_cast<float>(record.launch_degrees);
  out.fire_frames = static_cast<float>(record.fire_frames);
  out.life_frames = static_cast<float>(record.life_frames);
  out.start_speed = record.start_speed;
  out.top_speed = record.top_speed;
  out.acceleration = record.acceleration;
  out.spread_degrees = static_cast<float>(record.spread_degrees);
  out.homing_degrees = record.homing_degrees.value(level);
  if (out.homing_degrees > 0.0F) {
    out.homing_degrees = std::max(out.homing_degrees, record.homing_min_degrees);
  }
  out.rounds = truncated(record.rounds, level, record.rounds_min);
  out.damage = truncated(record.damage, level, record.damage_min);
  out.blast_radius = record.blast_radius.value(level);
  out.blast_start_radius = static_cast<float>(record.blast_start_radius);
  out.blast_frames = record.blast_frames.value(level);
  out.blast_damage = record.blast_damage.value(level);
  out.ammo = truncated(record.ammo, level, record.ammo_min);
  out.recharge_frames = record.recharge_frames.value(level);
  out.stun = record.stun.value(level);
  out.knockback = record.knockback.value(level);
  out.knockdown = record.knockdown.value(level);
  out.price = truncated(record.price, level);
  return out;
}

std::vector<std::string> apply_weapon_records(std::map<std::string, WeaponTuning> &weapons,
                                              const PartTables &tables) {
  std::vector<std::string> missing;
  for (auto &[name, weapon] : weapons) {
    if (!weapon.part) {
      continue;
    }
    const WeaponRecord *record = tables.weapon(*weapon.part);
    if (record == nullptr) {
      missing.push_back(name);
      continue;
    }
    apply_weapon_stats(weapon, weapon_stats(*record, weapon.part->level));
    weapon.source_melee_kind = record->source_melee_kind;
  }
  return missing;
}

} // namespace phantomcrash
