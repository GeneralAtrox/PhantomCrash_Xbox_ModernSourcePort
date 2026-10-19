// Parsing of a location menu: data/ui/club_wired.json, data/ui/garage.json.

#include "phantomcrash/location_menu.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>

namespace phantomcrash {
namespace {

using json::numbers;
using json::required;

// A sprite member that takes the image from `image` when it has none.
Sprite item_sprite(const json::JsonValue &item, const char *key, const std::string &image) {
  try {
    const auto &value = required(item, key);
    Sprite sprite;
    sprite.id = std::string(key);
    sprite.image = value.string_or("image", image);
    sprite.rect = numbers<4>(required(value, "rect"), "rect");
    if (const auto *position = value.find("position")) {
      sprite.position = numbers<2>(*position, "position");
    }
    sprite.layer = static_cast<int>(required(value, "layer").number());
    return sprite;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string(key) + ": " + error.what());
  }
}

LocationItem parse_item(const json::JsonValue &value, const std::string &image) {
  LocationItem item;
  item.id = required(value, "id").string();
  item.next = required(value, "next").string();
  item.help = parse_help(value);
  item.wedge = item_sprite(value, "wedge", image);
  item.highlight = item_sprite(value, "highlight", image);
  item.label = item_sprite(value, "label", image);
  item.select_sound = value.string_or("select_sound", "");
  return item;
}

LocationWheel parse_wheel(const json::JsonValue &value) {
  LocationWheel wheel;
  wheel.position = numbers<2>(required(value, "position"), "position");
  wheel.pivot = numbers<2>(required(value, "pivot"), "pivot");
  wheel.step_degrees = static_cast<float>(required(value, "step_degrees").number());
  wheel.slots = static_cast<int>(required(value, "slots").number());
  wheel.turn_seconds = required(value, "turn_seconds").number();
  if (wheel.slots < 1 || wheel.turn_seconds <= 0.0) {
    throw std::runtime_error("wheel needs at least one slot and a turn longer than 0");
  }
  return wheel;
}

std::map<std::string, std::string> string_map(const json::JsonValue &value) {
  std::map<std::string, std::string> result;
  for (const auto &[key, item] : value.object()) {
    result[key] = item.string();
  }
  return result;
}

} // namespace

LocationMenuScript parse_location_menu(const json::JsonValue &document, const std::string &name) {
  try {
    LocationMenuScript script;
    parse_menu_basics(document, script);
    script.scene = static_cast<int>(required(document, "scene").number());
    script.help = parse_ticker(required(document, "help"));
    script.frame = parse_frame_placement(required(document, "frame"));
    script.wheel = parse_wheel(required(document, "wheel"));
    const auto &buttons = required(document, "buttons");
    script.back_buttons = buttons_from_json(required(buttons, "back"));
    script.forward_buttons = buttons_from_json(required(buttons, "forward"));
    const std::string image = required(document, "image").string();
    for (const auto &value : required(document, "items").array()) {
      try {
        script.items.push_back(parse_item(value, image));
      } catch (const std::exception &error) {
        throw std::runtime_error("item \"" + value.string_or("id", "?") + "\": " + error.what());
      }
    }
    if (script.items.empty()) {
      throw std::runtime_error("items needs at least one item");
    }
    script.cancel_item = required(document, "cancel_item").string();
    if (const auto *start = document.find("start_items")) {
      script.start_items = string_map(*start);
    }
    if (const auto *arrive = document.find("arrive_sounds")) {
      script.arrive_sounds = string_map(*arrive);
    }
    if (const auto *shortcuts = document.find("shortcuts")) {
      for (const auto &shortcut : shortcuts->array()) {
        const std::string item = required(shortcut, "item").string();
        if (std::ranges::none_of(script.items,
                                 [&](const LocationItem &entry) { return entry.id == item; })) {
          throw std::runtime_error("shortcut to unknown item \"" + item + "\"");
        }
        script.shortcuts.emplace_back(buttons_from_json(required(shortcut, "buttons")), item);
      }
    }
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(name + ": " + error.what());
  }
}

} // namespace phantomcrash
