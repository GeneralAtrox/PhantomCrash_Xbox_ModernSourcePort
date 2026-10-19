// Parsing of data/ui/sv_spec.json.

#include "phantomcrash/sv_spec_script.hpp"

#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/menu_screen.hpp"

#include <map>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
namespace {

using json::member;
using json::numbers;
using json::required;

float number_of(const json::JsonValue &value, const char *key) {
  return static_cast<float>(required(value, key).number());
}

int integer_of(const json::JsonValue &value, const char *key) {
  return static_cast<int>(required(value, key).number());
}

std::uint32_t colour_of(const json::JsonValue &value, const char *key) {
  return parse_colour(required(value, key).string());
}

SpecStat stat_named(const std::string &name) {
  static const std::map<std::string, SpecStat> stats{
      {"armour", SpecStat::armour},       {"walk", SpecStat::walk},
      {"dash", SpecStat::dash},           {"jump", SpecStat::jump},
      {"overclock", SpecStat::overclock}, {"weight", SpecStat::weight},
      {"capacity", SpecStat::capacity},   {"sockets", SpecStat::sockets},
  };
  const auto found = stats.find(name);
  if (found == stats.end()) {
    throw std::runtime_error("no vehicle line is named " + name);
  }
  return found->second;
}

CombatStat combat_stat_named(const std::string &name) {
  static const std::map<std::string, CombatStat> stats{
      {"mortal_shot", CombatStat::mortal_shot},
      {"repulsion_shield", CombatStat::repulsion_shield},
      {"knockdown_resistance", CombatStat::knockdown_resistance},
      {"knockback_resistance", CombatStat::knockback_resistance},
      {"laser_reduction", CombatStat::laser_reduction},
      {"charge_time", CombatStat::charge_time},
      {"lock_time", CombatStat::lock_time},
      {"aim_speed", CombatStat::aim_speed},
  };
  const auto found = stats.find(name);
  if (found == stats.end()) {
    throw std::runtime_error("no combat line is named " + name);
  }
  return found->second;
}

SpecPageKind page_named(const std::string &name) {
  if (name == "vehicle") {
    return SpecPageKind::vehicle;
  }
  if (name == "weapons") {
    return SpecPageKind::weapons;
  }
  if (name == "combat") {
    return SpecPageKind::combat;
  }
  throw std::runtime_error("no page shows " + name);
}

PartSlot slot_named(const std::string &name) {
  static const std::map<std::string, PartSlot> slots{
      {"right_arm", PartSlot::right_arm},
      {"left_arm", PartSlot::left_arm},
      {"right_shoulder", PartSlot::right_shoulder},
      {"left_shoulder", PartSlot::left_shoulder},
      // The older names, which called side 0 left.
      {"arm_left", PartSlot::right_arm},
      {"arm_right", PartSlot::left_arm},
      {"shoulder_left", PartSlot::right_shoulder},
      {"shoulder_right", PartSlot::left_shoulder},
  };
  const auto found = slots.find(name);
  if (found == slots.end()) {
    throw std::runtime_error("no weapon slot is named " + name);
  }
  return found->second;
}

void parse_window(const json::JsonValue &document, SvSpecScript &script) {
  const auto &names = required(document, "names");
  script.name_at = numbers<2>(required(names, "name_at"), "name_at");
  script.chip_name_at = numbers<2>(required(names, "chip_name_at"), "chip_name_at");
  script.name_colour = colour_of(names, "colour");
  script.name_layer = integer_of(names, "layer");
  script.name_glyphs = static_cast<int>(names.number_or("max_glyphs", 9));
  script.maker_at = numbers<2>(required(document, "maker_at"), "maker_at");
  script.maker_icons = member(document, "maker_icons", parse_sprites);
  const auto arrows = member(document, "arrows", parse_sprites);
  if (arrows.size() != 2) {
    throw std::runtime_error("arrows: needs a left and a right");
  }
  script.arrows = {arrows[0], arrows[1]};
  script.close_hint = member(document, "close_hint", parse_sprites);
  const auto &pages = required(document, "pages").array();
  if (pages.empty()) {
    throw std::runtime_error("pages: needs one at least");
  }
  for (std::size_t index = 0; index < pages.size(); ++index) {
    SpecPage page;
    // Without "show", the original's order: the vehicle's, then the weapons'.
    page.kind = index == 0 ? SpecPageKind::vehicle : SpecPageKind::weapons;
    if (const auto *show = pages[index].find("show")) {
      page.kind = page_named(show->string());
    }
    if (pages[index].find("panel") != nullptr) {
      page.panel = member(pages[index], "panel", parse_sprite);
    }
    page.help = required(pages[index], "help").string();
    script.pages.push_back(std::move(page));
  }
}

void parse_combat(const json::JsonValue &value, SvSpecScript &script) {
  CombatLettering &lettering = script.combat_lettering;
  lettering.font = required(value, "font").string();
  lettering.unit_font = value.string_or("unit_font", lettering.font);
  lettering.colour = colour_of(value, "colour");
  if (const auto *shadow = value.find("shadow")) {
    lettering.shadow = parse_text_shadow(*shadow);
    lettering.shadow_blur = static_cast<float>(shadow->number_or("blur", 0.0));
  }
  lettering.origin = numbers<2>(required(value, "origin"), "origin");
  lettering.label_x = numbers<2>(required(value, "label_x"), "label_x");
  lettering.unit_x = numbers<2>(required(value, "unit_x"), "unit_x");
  lettering.ink = numbers<2>(required(value, "ink"), "ink");
  for (const json::JsonValue &row : required(value, "rows").array()) {
    CombatRow line{combat_stat_named(required(row, "stat").string()),
                   required(row, "label").string(), row.string_or("unit", ""),
                   static_cast<int>(row.number_or("decimals", 0))};
    if (const auto *ink = row.find("unit_ink")) {
      line.unit_ink = numbers<2>(*ink, "unit_ink");
    }
    script.combat_rows.push_back(std::move(line));
  }
}

void parse_vehicle(const json::JsonValue &value, SvSpecScript &script) {
  script.values_right = number_of(value, "right");
  script.values_top = number_of(value, "top");
  script.values_step = number_of(value, "step");
  script.value_colour = colour_of(value, "colour");
  script.value_layer = integer_of(value, "layer");
  for (const json::JsonValue &row : required(value, "rows").array()) {
    script.rows.push_back(
        {stat_named(required(row, "stat").string()), integer_of(row, "decimals")});
  }
}

void parse_weapons(const json::JsonValue &value, SvSpecScript &script) {
  script.weapons_x = number_of(value, "x");
  for (const json::JsonValue &row : required(value, "rows").array()) {
    script.weapon_rows.push_back(static_cast<float>(row.number()));
  }
  for (const json::JsonValue &slot : required(value, "slots").array()) {
    script.weapon_slots.push_back(slot_named(slot.string()));
  }
  if (script.weapon_slots.size() != script.weapon_rows.size()) {
    throw std::runtime_error("weapons: needs a row for each slot");
  }
  script.weapon_colour = colour_of(value, "colour");
  script.none_text = required(value, "none").string();
  script.none_colour = colour_of(value, "none_colour");
  script.weapon_layer = integer_of(value, "layer");
}

} // namespace

SvSpecScript parse_sv_spec(const json::JsonValue &document) {
  try {
    SvSpecScript script;
    parse_town_basics(document, script);
    const auto &buttons = required(document, "buttons");
    script.left = buttons_from_json(required(buttons, "left"));
    script.right = buttons_from_json(required(buttons, "right"));
    const auto &sounds = required(document, "sounds");
    script.page_sound = sounds.string_or("page", "");
    script.back_sound = sounds.string_or("back", "");
    parse_window(document, script);
    parse_vehicle(required(document, "vehicle"), script);
    parse_weapons(required(document, "weapons"), script);
    if (const auto *combat = document.find("combat")) {
      parse_combat(*combat, script);
    }
    for (const SpecPage &page : script.pages) {
      if (page.kind == SpecPageKind::combat && script.combat_rows.empty()) {
        throw std::runtime_error("pages: a combat page needs the combat lines");
      }
    }
    script.help_style = member(document, "help", parse_ticker);
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("sv_spec.json: ") + error.what());
  }
}

} // namespace phantomcrash
