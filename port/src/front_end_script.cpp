// Parsing of data/ui/title.json and data/ui/main_menu.json.

#include "phantomcrash/front_end.hpp"

#include "phantomcrash/json_fields.hpp"

#include <stdexcept>

namespace phantomcrash {
namespace {

using json::numbers;
using json::required;

// An item's sprites take the item's position.
Sprite item_sprite(const json::JsonValue &item, const char *key) {
  Sprite sprite = parse_sprite(required(item, key));
  if (const auto *position = item.find("position")) {
    sprite.position = numbers<2>(*position, "position");
  }
  return sprite;
}

MenuItem parse_item(const json::JsonValue &value) {
  MenuItem item;
  item.id = value.string_or("id", "");
  try {
    item.normal = item_sprite(value, "normal");
    item.label = value.string_or("label", "");
    item.selected = item_sprite(value, "selected");
    item.submenu = value.string_or("submenu", "");
    item.go_to = value.string_or("goto", "");
    if (item.submenu.empty() == item.go_to.empty()) {
      throw std::runtime_error("needs either \"submenu\" or \"goto\"");
    }
    item.requires_controllers = static_cast<int>(value.number_or("requires_controllers", 0));
    if (const auto *colour = value.find("unavailable_colour")) {
      item.unavailable_colour = parse_colour(colour->string());
    }
  } catch (const std::exception &error) {
    throw std::runtime_error("item \"" + item.id + "\": " + error.what());
  }
  return item;
}

Menu parse_menu(const json::JsonValue &value) {
  Menu menu;
  for (const auto &item : required(value, "items").array()) {
    menu.items.push_back(parse_item(item));
  }
  if (menu.items.empty()) {
    throw std::runtime_error("needs at least one item");
  }
  if (const auto *hints = value.find("hints")) {
    menu.hints = parse_sprites(*hints);
  }
  menu.default_cursor = static_cast<int>(value.number_or("default_cursor", 0));
  if (menu.default_cursor < 0 || menu.default_cursor >= static_cast<int>(menu.items.size())) {
    throw std::runtime_error("default_cursor is not an item");
  }
  return menu;
}

} // namespace

TitleScript parse_title(const json::JsonValue &document) {
  try {
    TitleScript title;
    title.layers = parse_sprites(required(document, "layers"));
    title.music = document.string_or("music", "");
    title.fade_in = document.number_or("fade_in", 0.0);
    const auto &start = required(document, "start");
    title.start_buttons = buttons_from_json(required(start, "buttons"));
    title.start_sound = start.string_or("sound", "");
    const auto &idle = required(document, "idle");
    title.idle_seconds = required(idle, "seconds").number();
    title.idle_fade_out = idle.number_or("fade_out", 0.0);
    title.idle_goto = required(idle, "goto").string();
    return title;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("title.json: ") + error.what());
  }
}

MainMenuScript parse_main_menu(const json::JsonValue &document) {
  try {
    MainMenuScript menu;
    menu.music = document.string_or("music", "");
    menu.main = parse_menu(document);
    if (const auto *submenus = document.find("submenus")) {
      for (const auto &[name, value] : submenus->object()) {
        try {
          menu.submenus.emplace(name, parse_menu(value));
        } catch (const std::exception &error) {
          throw std::runtime_error("submenu \"" + name + "\": " + error.what());
        }
      }
    }
    const auto check = [&](const Menu &level) {
      for (const auto &item : level.items) {
        if (!item.submenu.empty() && !menu.submenus.contains(item.submenu)) {
          throw std::runtime_error("item \"" + item.id + "\": no submenu \"" + item.submenu + "\"");
        }
      }
    };
    check(menu.main);
    for (const auto &[name, level] : menu.submenus) {
      check(level);
    }
    menu.fade_in = document.number_or("fade_in", 0.0);
    menu.fade_out = document.number_or("fade_out", 0.0);
    const auto &idle = required(document, "idle");
    menu.idle_seconds = required(idle, "seconds").number();
    menu.idle_resets_on_input = idle.boolean_or("resets_on_input", false);
    menu.confirm_buttons = buttons_from_json(required(required(document, "confirm"), "buttons"));
    menu.cancel_buttons = buttons_from_json(required(required(document, "cancel"), "buttons"));
    if (const auto *repeat = document.find("cursor_repeat")) {
      menu.repeat_delay = required(*repeat, "delay").number();
      menu.repeat_interval = required(*repeat, "interval").number();
    }
    const auto &sounds = required(document, "sounds");
    menu.move_sound = sounds.string_or("move", "");
    menu.select_sound = sounds.string_or("select", "");
    menu.cancel_sound = sounds.string_or("cancel", "");
    menu.unavailable_sound = sounds.string_or("unavailable", "");
    return menu;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("main_menu.json: ") + error.what());
  }
}

} // namespace phantomcrash
