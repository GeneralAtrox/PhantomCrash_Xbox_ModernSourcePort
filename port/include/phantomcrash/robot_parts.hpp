#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/movement_tuning.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

struct WeaponTuning;

// A part level runs from -99 to 99 (0x19cbfc); level 0 is the default.
inline constexpr int part_level_max = 99;
// The records count time in the game's 60 Hz frames.
inline constexpr float part_frames_per_second = 60.0F;
// The battle logic runs at 30 Hz (scene 42, 0x255ab0): per-tick values
// convert to seconds by this.
inline constexpr float battle_ticks_per_second = 30.0F;
// A weapon record's flight (+0x1d, table 0x2533f4): 0 straight, 1 lobbed,
// 2 missile.
inline constexpr int weapon_flight_lobbed = 1;
inline constexpr int weapon_flight_missile = 2;

// How a part's value grows with its level (0x092160 for 16-bit curves,
// 0x0924fd for float ones): up to the knee it climbs from `base` by
// `mid`, from the knee to level 99 on to `base + top`. A knee of 0 climbs
// straight to `top` and a knee of 99 to `mid`. A negative level runs the
// same curve by its size, scaled by `low_scale`. Never below zero.
struct LevelCurve {
  float base{};
  float mid{};
  float top{};
  int knee{};
  int low_scale{-1};

  [[nodiscard]] float value(int level) const;
};

// An AI chip record (0x1b7fc8, 0x7c bytes): the robot's fire control,
// copied to the robot at +0x4504 by 0x092c6c. Field names are
// researcher-assigned.
struct ChipRecord {
  int index{};
  std::string name;
  int inner_shape{}; // 0 square, 1 wide, 2 tall
  LevelCurve inner_value;
  int track{}; // 0..999: how fast the reticle follows a target
  int outer_shape{};
  LevelCurve outer_value;
  int lock_frames{}; // a lock takes this many 60 Hz frames
  // The battle chatter (battle_chatter.hpp): the line table a player's
  // robot speaks from (+0x18) and the face its name entry shows (+0x1a).
  int chatter{-1};
  std::optional<Sprite> face;
  // The robot's fight (0x092c6c +0x24..+0x2c, read by 0x0988e1).
  LevelCurve critical_percent;     // +0x3c
  LevelCurve dodge_percent;        // +0x4c
  LevelCurve knockdown_resistance; // +0x5c
  LevelCurve weight;               // +8 (0x075f93)
  LevelCurve price;                // +0x6c (0x0932c7)
};

// An arm (0x1bae48) or shoulder (0x1be9a8) weapon record, 0xc8 bytes, as
// 0x092a07 reads it (docs/PART_RECORDS.md). Times are in 60 Hz frames and
// speeds in units a frame. Field names are researcher-assigned.
struct WeaponRecord {
  int index{};
  std::string name;
  int kind{};                // +0x1c: the HUD's weapon kind (see WeaponTuning::hud_kind)
  int source_melee_kind{-1}; // original S.L.A.I. arm-action family, separate from adapted HUD kind
  int flight{};              // +0x1d: 0 straight, 1 lobbed, 2 missile
  int maker{};               // +4 ("family"), as a body's (0x01ede8)
  int model{-1};             // +8: its model, which some fire sounds go by
  int fire_sound{};          // +0x21: its fire sound's handler (table 0x2537c0)
  int look{-1};              // +0x22: how its shots are drawn (effects.json projectiles)
  int surface_hit{};         // +0x1f: what a shot does on a wall or the floor (0x098eda)
  int robot_hit{};           // +0x20: ... and on a robot
  int impact{-1};            // +0x23: the impact it starts (effects.json impacts)
  int launch_degrees{};
  int fire_frames{}; // between shots
  int life_frames{}; // a projectile's lifetime
  float start_speed{};
  float top_speed{};
  float acceleration{}; // a missile's, per frame
  int spread_degrees{}; // of the extra rounds
  float homing_min_degrees{};
  LevelCurve homing_degrees; // +0x40: a missile's turn, per frame
  int rounds_min{1};
  LevelCurve rounds; // per trigger pull
  int damage_min{};
  LevelCurve damage;
  LevelCurve blast_radius;
  int blast_start_radius{};
  LevelCurve blast_frames; // the blast grows over this long
  LevelCurve blast_damage;
  int ammo_min{};
  LevelCurve ammo;            // +0x80
  LevelCurve recharge_frames; // +0x88: a beam's, from empty to full
  LevelCurve stun;            // +0x90: over 0 makes the target flinch
  LevelCurve knockback;       // +0x98: units a tick
  LevelCurve knockdown;       // +0xa8: percent, on a critical hit
  LevelCurve price;
  LevelCurve weight; // +0x0c (0x075f93)
  int aim_mode{-1};  // +0x1e: 0/1 shoulder servos, other values keep mount direction
};

// A body (0x1b5b88) or legs (0x1b5f88) record: the armour it gives, never
// below its floor, which differs for players' and computer robots
// (0x076114).
struct ArmourRecord {
  std::optional<WheeledTuning> wheels;
  int index{};
  std::string name;
  // The maker (+4; a body's "class", legs' "maker"): 0 Holy, 1 Photon,
  // 2 Aeron, then the makers only a won contest opens (0x05f750).
  int maker{};
  LevelCurve armour;
  float player_floor{};
  float computer_floor{};
  LevelCurve knockback_resistance; // legs +0x7c, percent; bodies have none
  LevelCurve weight;               // body +0x18, legs +0x4c (0x075f93)
  LevelCurve price;                // body +0x30, legs +0x8c (0x0932c7)
  LevelCurve sockets;              // body +0x28: socket holes for modules (0x0bb0c8)
  // Legs only (robot_load.hpp): the model, and the curves the load scales.
  int model{-1};
  LevelCurve capacity;     // +0x5c: the weight carried before the robot slows
  LevelCurve speed;        // +0x1c: the dash; the walk is half
  LevelCurve jump;         // +0x2c: the jump's height
  LevelCurve turn;         // +0x3c
  LevelCurve dodge;        // +0x9c: a dodge's push on the ground ...
  LevelCurve air_dodge;    // +0xac: ... and in the air
  float stop_keep{0.875F}; // legs +0xbc: retained stopping momentum per 30 Hz tick
};

// A pilot (0x1c9920): the tag its robot's marker shows (0x04de50), and
// what WIRE HEADS DATA shows (0x013f5a): the names and faces of the pilot
// and of the second name entry (robot_name, the pilot's partner), the team
// (empty for none) and the bounty in NY by level (+8, 0x092311).
struct PilotRecord {
  int index{};
  std::string name;
  std::string tag;
  std::string robot_name;
  std::optional<Sprite> face;
  std::optional<Sprite> robot_face;
  std::string team;
  LevelCurve bounty;
};

enum class WeaponTable { arm, shoulder };

// Which record a weapon is, and at what level.
struct PartRef {
  WeaponTable table{WeaponTable::arm};
  int index{};
  int level{};
};

// The part records the extractor writes to tables/parts.json.
struct PartTables {
  std::vector<ChipRecord> chips;
  std::vector<WeaponRecord> arm_weapons;
  std::vector<WeaponRecord> shoulder_weapons;
  std::vector<ArmourRecord> bodies;
  std::vector<ArmourRecord> legs;
  // Per legs model, the walk and dash speeds its motions cover at their
  // own rate (0x232558); 0 where it has no root motion.
  std::vector<std::array<float, 2>> leg_motion_speeds;
  std::vector<PilotRecord> pilots;

  [[nodiscard]] const ChipRecord *chip(int index) const;
  [[nodiscard]] const WeaponRecord *weapon(const PartRef &part) const;
  [[nodiscard]] const PilotRecord *pilot(int index) const;
};

// A robot's armour (0x076114): its body's and legs' armour at their
// levels, each raised to its floor. Missing parts give nothing. The
// option parts, which add to it, are not ported.
[[nodiscard]] float robot_armour(const PartTables &tables, int body, int body_level, int legs,
                                 int legs_level, bool player);

// What a robot's chip and legs give it in a fight, as the damage rules
// (0x0988e1) read them: the chip's block (0x075d4a) and the legs' +0x7c.
// The fitted modules add to the critical and knockdown resistance and give
// the beam resistance (add_module_bonuses in garage_tables.hpp).
struct CombatStats {
  float critical_percent{};     // a hit it lands doubles when rand(0..100) is below
  float dodge_chance{};         // 0..1: a hit on it misses when rand(0..1) is below
  float knockdown_resistance{}; // taken off a weapon's knockdown chance
  float knockback_resistance{}; // percent of a knockback it shrugs off
  float beam_resistance{};      // 0..1 of a beam's damage it shrugs off (+0x4544)
  // Negative means no module data: use the scripted battle defaults.
  float camouflage_seconds{-1.0F};
  float camouflage_recharge_seconds{-1.0F};
};

// A missing chip or legs gives nothing.
[[nodiscard]] CombatStats combat_stats(const PartTables &tables, int chip, int chip_level, int legs,
                                       int legs_level);

// Throws std::runtime_error naming what is malformed.
[[nodiscard]] PartTables parse_part_tables(const json::JsonValue &document);

// {"table": "arm" | "shoulder", "index": n, "level": n}.
[[nodiscard]] PartRef parse_part_ref(const json::JsonValue &value);

// A chip at a level, as the HUD and the locks use it.
struct ChipStats {
  int inner_shape{};
  float inner_value{};
  int outer_shape{};
  float outer_value{};
  float track{};
  float lock_frames{};
};

[[nodiscard]] ChipStats chip_stats(const ChipRecord &chip, int level);

// A weapon record at a level, as 0x092a07 builds the weapon's stats
// block (weapon +0x20). Integer values are truncated as the game does.
struct WeaponStats {
  int hud_kind{};
  int flight{};
  int model{-1};
  int fire_sound{-1};
  int look{-1};
  int surface_hit{};
  int robot_hit{};
  int impact{-1};
  float launch_degrees{};
  float fire_frames{};
  float life_frames{};
  float start_speed{};
  float top_speed{};
  float acceleration{};
  float spread_degrees{};
  float homing_degrees{}; // per frame; 0 does not home
  int rounds{1};
  int damage{};
  float blast_radius{};
  float blast_start_radius{};
  float blast_frames{};
  float blast_damage{};
  int ammo{};
  float recharge_frames{};
  float stun{};
  float knockback{};
  float knockdown{};
  int price{};
  int aim_mode{-1};
};

[[nodiscard]] WeaponStats weapon_stats(const WeaponRecord &record, int level);

// Converts a record's stats to the battle's seconds and units a second.
void apply_weapon_stats(WeaponTuning &weapon, const WeaponStats &stats);

// Gives every weapon naming a part its record's stats: HUD kind, damage,
// timing, flight, blast and ammunition, converted to seconds and units a
// second. Returns the weapons whose record is missing, left as they were.
[[nodiscard]] std::vector<std::string>
apply_weapon_records(std::map<std::string, WeaponTuning> &weapons, const PartTables &tables);

} // namespace phantomcrash
