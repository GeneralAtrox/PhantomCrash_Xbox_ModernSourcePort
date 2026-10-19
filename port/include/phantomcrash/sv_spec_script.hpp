#pragma once

#include "phantomcrash/garage.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/sprite_text.hpp"
#include "phantomcrash/town_screen.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// A line of SV SPEC's vehicle page (0x03bc25).
enum class SpecStat {
  armour,    // TOTAL DURABILITY (0x076114)
  walk,      // RUN SPEED, km/h (0x07c27d)
  dash,      // MAX SPEED, km/h
  jump,      // JUMP HEIGHT, m
  overclock, // TOTAL O.C TIME: the modules' active times added, seconds (0x05f8b3)
  weight,    // TOTAL WEIGHT, t (0x075f93)
  capacity,  // WEIGHT CAPACITY: the legs', t
  sockets,   // SOCKET HOLE: the body's (0x05f97c)
};

struct SpecRow {
  SpecStat stat{SpecStat::armour};
  int decimals{};
};

// A line of the COMBAT page (a port addition): what the robot brings to a
// fight, which the original shows nowhere but in the CHIP shop.
enum class CombatStat {
  mortal_shot,          // the CHIP's critical chance and the modules', percent
  repulsion_shield,     // the CHIP's dodge chance, percent (0x075d4a: 0..100)
  knockdown_resistance, // the CHIP's and the modules', percent
  knockback_resistance, // the legs', percent
  laser_reduction,      // the modules', percent (0x0b9eef)
  charge_time,          // the modules' O.C charge times added, seconds
  lock_time,            // the CHIP's lock, seconds
  aim_speed,            // the CHIP's reticle speed (0..999)
};

struct CombatRow {
  CombatStat stat{CombatStat::mortal_shot};
  std::string label;
  std::string unit;
  int decimals{};
  // The unit's ink, top and bottom in the line, where it is not the
  // label's (lower case sits lower: the art's "sec").
  std::optional<std::array<float, 2>> unit_ink;
};

// The COMBAT page's lettering: labels and units in a sprite_text.json
// display font, as the other pages' art letters them. `origin` is the
// panel's top-left; label_x, unit_x and ink are from it and from each
// line's top, the lines `values_step` apart as the vehicle page's.
struct CombatLettering {
  std::string font;
  std::string unit_font;
  std::uint32_t colour{0xffffff};
  std::optional<TextShadow> shadow;
  float shadow_blur{};
  std::array<float, 2> origin{};
  std::array<float, 2> label_x{};
  std::array<float, 2> unit_x{};
  std::array<float, 2> ink{};
};

// What a page shows: the original's vehicle and weapons pages, and the
// port's combat page.
enum class SpecPageKind { vehicle, weapons, combat };

// A page: its panel art (none for the combat page, which letters its own)
// and the help line saying how to turn to the others.
struct SpecPage {
  SpecPageKind kind{SpecPageKind::vehicle};
  std::optional<Sprite> panel;
  std::string help;
};

// SV SPEC (scene 36): what data/ui/sv_spec.json holds.
struct SvSpecScript : TownBasics {
  Buttons left{};
  Buttons right{};
  std::string page_sound;
  std::string back_sound;

  // The SV's and its CHIP's names in the window (0x03ba94).
  std::array<float, 2> name_at{};
  std::array<float, 2> chip_name_at{};
  std::uint32_t name_colour{0xffffff};
  int name_layer{};
  int name_glyphs{9};
  // The body maker's badge, by maker.
  std::array<float, 2> maker_at{};
  std::vector<Sprite> maker_icons;
  std::array<Sprite, 2> arrows;
  std::vector<Sprite> close_hint;
  std::vector<SpecPage> pages; // right turns to the next, left to the one before

  // Page 0: the lines right-aligned from `top`, `step` apart.
  std::vector<SpecRow> rows;
  float values_right{};
  float values_top{};
  float values_step{};
  std::uint32_t value_colour{0xffffff};
  int value_layer{};

  // Page 1: each slot's weapon at `weapons_x`, the rows' heights; "none"
  // in its own colour for an empty slot.
  std::vector<PartSlot> weapon_slots;
  std::vector<float> weapon_rows;
  float weapons_x{};
  std::uint32_t weapon_colour{0xffffff};
  std::string none_text;
  std::uint32_t none_colour{0xffffff};
  int weapon_layer{};

  // The combat page's lines, their values placed as the vehicle page's.
  std::vector<CombatRow> combat_rows;
  CombatLettering combat_lettering;

  TickerStyle help_style;
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] SvSpecScript parse_sv_spec(const json::JsonValue &document);

} // namespace phantomcrash
