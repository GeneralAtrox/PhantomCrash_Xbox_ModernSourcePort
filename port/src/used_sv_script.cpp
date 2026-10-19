// Parsing of data/ui/used_sv.json.

#include "phantomcrash/used_sv_script.hpp"

#include "phantomcrash/json_fields.hpp"

#include <charconv>
#include <stdexcept>

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

double seconds_of(const json::JsonValue &value, const char *key) {
  const double seconds = required(value, key).number();
  if (seconds <= 0.0) {
    throw std::runtime_error(std::string(key) + " must be above 0");
  }
  return seconds;
}

void parse_slots(const json::JsonValue &value, UsedSvScript &script) {
  for (const auto &slot : required(value, "at").array()) {
    script.slots.push_back(numbers<2>(slot, "at"));
  }
  for (const auto &x : required(value, "slide_from").array()) {
    script.slot_from.push_back(static_cast<float>(x.number()));
  }
  script.slot_speed = number_of(value, "slide_speed");
  script.icon_layer = integer_of(value, "layer");
  script.lit_layer = integer_of(value, "lit_layer");
  script.exit = member(value, "exit", parse_shop_icon);
  for (const auto &[key, icon] : required(value, "legs_icons").object()) {
    int legs = -1;
    std::from_chars(key.data(), key.data() + key.size(), legs);
    script.legs_icons[legs] = parse_shop_icon(icon);
  }
  script.default_icon = member(value, "default_icon", parse_shop_icon);
  if (script.slots.size() < 2 || script.slot_from.size() != script.slots.size() ||
      script.slot_speed <= 0.0F) {
    throw std::runtime_error("slots: at least one SV slot and EXIT's, each with slide_from");
  }
}

void parse_info(const json::JsonValue &value, UsedSvScript &script) {
  script.info_panel = member(value, "panel", parse_sprite);
  script.info_growth = member(value, "growth", parse_popup_growth);
  script.badges = member(value, "badges", parse_sprites);
  const auto &name = required(value, "name");
  script.name_at = numbers<2>(required(name, "position"), "position");
  script.name_colour = parse_colour(required(name, "colour").string());
  script.name_layer = integer_of(name, "layer");
  script.price_label = member(value, "price_label", parse_sprite);
  script.price = member(value, "price", parse_number_style);
}

} // namespace

UsedSvScript parse_used_sv(const json::JsonValue &document) {
  try {
    UsedSvScript script;
    parse_town_basics(document, script);
    parse_sv_purchase(document, script, script.purchase);
    const auto &opening = required(document, "opening");
    script.strip_seconds = seconds_of(opening, "strip_seconds");
    script.plate_seconds = seconds_of(opening, "plate_seconds");
    script.plate_from = number_of(opening, "plate_from");
    script.open_sound = required(document, "sounds").string_or("open", "");
    script.strip = member(document, "strip", parse_sprite);
    script.plate = member(document, "plate", parse_sprite);
    script.gears = member(document, "gears", parse_sprites);
    script.gear_spin = number_of(document, "gear_spin");
    parse_slots(required(document, "slots"), script);
    parse_info(required(document, "info"), script);
    const auto &gate = required(document, "maker_gate");
    script.gated_maker = integer_of(gate, "from_maker");
    script.unlock_contest = integer_of(gate, "contest");
    script.help_style = member(document, "help", parse_ticker);
    for (const auto &[id, text] : required(document, "help_text").object()) {
      script.help[id] = text.string();
    }
    script.purchase.hangar_help = script.help["hangars"];
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("used_sv.json: ") + error.what());
  }
}

} // namespace phantomcrash
