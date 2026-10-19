// Parsing of data/ui/town_map.json.

#include "phantomcrash/town_map.hpp"

#include "phantomcrash/json_fields.hpp"

#include <stdexcept>

namespace phantomcrash {
namespace {

using json::numbers;
using json::required;

Sprite member_sprite(const json::JsonValue &value, const char *key) {
  try {
    return parse_sprite(required(value, key));
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string(key) + ": " + error.what());
  }
}

TownPlace parse_place(const json::JsonValue &value) {
  TownPlace place;
  place.id = required(value, "id").string();
  place.next = required(value, "next").string();
  place.help = parse_help(value);
  place.badge = member_sprite(value, "badge");
  place.arrows = member_sprite(value, "arrows");
  place.sign = member_sprite(value, "sign");
  place.sign_from = numbers<2>(required(value, "sign_from"), "sign_from");
  place.rider = numbers<2>(required(value, "rider"), "rider");
  place.rider_flipped = value.boolean_or("rider_flipped", false);
  place.hit = numbers<4>(required(value, "hit"), "hit");
  return place;
}

} // namespace

TownMapScript parse_town_map(const json::JsonValue &document) {
  try {
    TownMapScript script;
    parse_menu_basics(document, script);
    script.scene = static_cast<int>(required(document, "scene").number());
    script.help = parse_ticker(required(document, "help"));
    script.frame = parse_frame_placement(required(document, "frame"));
    const auto &rider = required(document, "rider");
    script.rider = member_sprite(document, "rider");
    script.bob = static_cast<float>(rider.number_or("bob", script.bob));
    script.bob_x_seconds = rider.number_or("bob_x_seconds", script.bob_x_seconds);
    script.bob_y_seconds = rider.number_or("bob_y_seconds", script.bob_y_seconds);
    script.pop_seconds = required(document, "pop_seconds").number();
    if (script.pop_seconds <= 0.0 || script.bob_x_seconds <= 0.0 || script.bob_y_seconds <= 0.0) {
      throw std::runtime_error("pop_seconds and the rider's bob times must be above 0");
    }
    const auto &buttons = required(document, "buttons");
    script.back_buttons = buttons_from_json(required(buttons, "back"));
    script.forward_buttons = buttons_from_json(required(buttons, "forward"));
    script.orient_to_start = buttons.boolean_or("orient_to_start", false);
    const auto &sounds = required(document, "sounds");
    script.place_sound = required(sounds, "place").string();
    script.spin_sound = sounds.string_or("spin", "");
    if (const auto *spin = document.find("spin")) {
      script.spin_pay = static_cast<int>(spin->number_or("pay", script.spin_pay));
      script.spin_limit = static_cast<int>(spin->number_or("limit", script.spin_limit));
    }
    for (const auto &value : required(document, "places").array()) {
      try {
        script.places.push_back(parse_place(value));
      } catch (const std::exception &error) {
        throw std::runtime_error("place \"" + value.string_or("id", "?") + "\": " + error.what());
      }
    }
    if (script.places.empty()) {
      throw std::runtime_error("places needs at least one place");
    }
    if (const auto *start = document.find("start_places")) {
      for (const auto &[screen, place] : start->object()) {
        script.start_places[screen] = place.string();
      }
    }
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("town_map.json: ") + error.what());
  }
}

} // namespace phantomcrash
