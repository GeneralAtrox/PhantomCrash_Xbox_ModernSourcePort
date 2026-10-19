// Parsing of data/ui/name_entry.json.

#include "phantomcrash/name_entry.hpp"

#include "phantomcrash/json_fields.hpp"

#include <stdexcept>

namespace phantomcrash {
namespace {

using json::numbers;
using json::required;

// A member that must hold one sprite.
Sprite sprite_at(const json::JsonValue &document, const char *key) {
  try {
    return parse_sprite(required(document, key));
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string(key) + ": " + error.what());
  }
}

NameEntryPage parse_page(const json::JsonValue &value) {
  NameEntryPage page;
  page.id = value.string_or("id", "");
  page.help = parse_help(value);
  if (const auto *rows = value.find("rows")) {
    for (const auto &row : rows->array()) {
      page.rows.push_back(split_characters(row.string()));
    }
  }
  return page;
}

void parse_pages(const json::JsonValue &document, NameEntryScript &script) {
  for (const auto &value : required(document, "pages").array()) {
    try {
      script.pages.push_back(parse_page(value));
    } catch (const std::exception &error) {
      throw std::runtime_error("page \"" + value.string_or("id", "?") + "\": " + error.what());
    }
  }
  if (script.pages.size() < 2 || !script.pages.back().rows.empty()) {
    throw std::runtime_error(
        "pages needs a page of characters and the End page, without rows, last");
  }
  // Every page of characters has the first page's shape.
  const auto &first = script.pages.front().rows;
  script.rows = static_cast<int>(first.size());
  script.columns = first.empty() ? 0 : static_cast<int>(first.front().size());
  if (script.rows == 0 || script.columns == 0) {
    throw std::runtime_error("the first page has no characters");
  }
  for (std::size_t index = 0; index + 1 < script.pages.size(); ++index) {
    const auto &page = script.pages[index];
    bool shaped = static_cast<int>(page.rows.size()) == script.rows;
    for (const auto &row : page.rows) {
      shaped = shaped && static_cast<int>(row.size()) == script.columns;
    }
    if (!shaped) {
      throw std::runtime_error("page \"" + page.id + "\" needs " + std::to_string(script.rows) +
                               " rows of " + std::to_string(script.columns) + " characters");
    }
  }
}

NameEntryAnimation parse_animation(const json::JsonValue &value) {
  NameEntryAnimation animation;
  const auto number = [&](const char *key, const auto fallback) {
    return static_cast<decltype(fallback)>(value.number_or(key, fallback));
  };
  animation.intro_per_second = number("intro_per_second", animation.intro_per_second);
  animation.slide_after = number("slide_after", animation.slide_after);
  animation.slide_from = number("slide_from", animation.slide_from);
  animation.slide_to = number("slide_to", animation.slide_to);
  animation.slide_per_second = number("slide_per_second", animation.slide_per_second);
  animation.hints_per_second = number("hints_per_second", animation.hints_per_second);
  animation.grow_below_x = number("grow_below_x", animation.grow_below_x);
  animation.grow_steps = number("grow_steps", animation.grow_steps);
  animation.grow_widen_steps = number("grow_widen_steps", animation.grow_widen_steps);
  animation.grow_overshoot = number("grow_overshoot", animation.grow_overshoot);
  animation.grow_start_height = number("grow_start_height", animation.grow_start_height);
  animation.page_per_second = number("page_per_second", animation.page_per_second);
  animation.pulse_per_second = number("pulse_per_second", animation.pulse_per_second);
  animation.pulse_alpha = number("pulse_alpha", animation.pulse_alpha);
  if (animation.grow_widen_steps <= 0 || animation.grow_steps <= animation.grow_widen_steps) {
    throw std::runtime_error(
        "animation: grow_steps must exceed grow_widen_steps, which must be > 0");
  }
  return animation;
}

NameEntryLayout parse_layout(const json::JsonValue &document) {
  NameEntryLayout layout;
  if (const auto *layers = document.find("intro_layers")) {
    layout.intro_layers = parse_sprites(*layers);
  }
  layout.slide_box = sprite_at(document, "slide_box");
  if (const auto *hints = document.find("hints")) {
    layout.hints = parse_sprites(*hints);
  }
  layout.name_box = sprite_at(document, "name_box");
  layout.label = sprite_at(document, "label");
  if (const auto *labels = document.find("labels")) {
    for (const auto &[target, label] : labels->object()) {
      layout.labels[target] = parse_sprite(label);
    }
  }
  const auto &name = required(document, "name");
  layout.name_position = numbers<2>(required(name, "position"), "name position");
  layout.name_advance = static_cast<float>(name.number_or("advance", layout.name_advance));
  layout.name_colour = parse_colour(name.string_or("colour", "#ffffff"));
  layout.name_layer = static_cast<int>(name.number_or("layer", layout.name_layer));
  const auto &grid = required(document, "grid");
  layout.grid_position = numbers<2>(required(grid, "position"), "grid position");
  layout.grid_step = numbers<2>(required(grid, "step"), "grid step");
  layout.grid_colour = parse_colour(grid.string_or("colour", "#c0c0c0"));
  layout.grid_layer = static_cast<int>(grid.number_or("layer", layout.grid_layer));
  layout.grid_cursor = sprite_at(document, "grid_cursor");
  layout.tab_cursor = sprite_at(document, "tab_cursor");
  layout.name_cursor = sprite_at(document, "name_cursor");
  layout.tab_step = static_cast<float>(required(document, "tab_step").number());
  return layout;
}

} // namespace

std::vector<std::string> split_characters(const std::string_view text) {
  std::vector<std::string> characters;
  for (const char byte : text) {
    // A UTF-8 continuation byte (10xxxxxx) belongs to the character before.
    if ((static_cast<unsigned char>(byte) & 0xc0U) == 0x80U && !characters.empty()) {
      characters.back() += byte;
    } else {
      characters.emplace_back(1, byte);
    }
  }
  return characters;
}

NameEntryScript parse_name_entry(const json::JsonValue &document) {
  try {
    NameEntryScript script;
    parse_menu_basics(document, script);
    script.help = parse_ticker(required(document, "help"));
    script.frame = parse_frame_placement(required(document, "frame"));
    const auto &buttons = required(document, "buttons");
    script.delete_buttons = buttons_from_json(required(buttons, "delete"));
    script.space_buttons = buttons_from_json(required(buttons, "space"));
    script.page_back_buttons = buttons_from_json(required(buttons, "page_back"));
    script.page_forward_buttons = buttons_from_json(required(buttons, "page_forward"));
    const auto &sounds = required(document, "sounds");
    script.move_x_sound = sounds.string_or("move_x", script.move_sound);
    script.page_sound = sounds.string_or("page", "");
    script.delete_sound = sounds.string_or("delete", "");
    script.max_length = static_cast<int>(document.number_or("max_length", script.max_length));
    if (script.max_length <= 0) {
      throw std::runtime_error("max_length must be at least 1");
    }
    parse_pages(document, script);
    if (const auto *keyboard = document.find("keyboard")) {
      if (const auto *aliases = keyboard->find("aliases")) {
        for (const auto &[typed, key] : aliases->object()) {
          script.typed_aliases[typed] = key.string();
        }
      }
    }
    script.no_name = parse_lines(required(document, "no_name"));
    if (const auto *animation = document.find("animation")) {
      script.animation = parse_animation(*animation);
    }
    script.layout = parse_layout(document);
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("name_entry.json: ") + error.what());
  }
}

} // namespace phantomcrash
