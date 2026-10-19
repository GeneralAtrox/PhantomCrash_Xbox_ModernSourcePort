// Parsing of data/ui/schedule.json.

#include "phantomcrash/schedule.hpp"

#include "phantomcrash/json_fields.hpp"

#include <stdexcept>

namespace phantomcrash {
namespace {

using json::member;
using json::numbers;
using json::required;

TexelRect rect_of(const json::JsonValue &value) {
  return numbers<4>(value, "rect");
}

std::array<float, 2> point_of(const json::JsonValue &value) {
  return numbers<2>(value, "position");
}

std::vector<int> integers(const json::JsonValue &value) {
  std::vector<int> out;
  for (const auto &item : value.array()) {
    out.push_back(static_cast<int>(item.number()));
  }
  return out;
}

ScheduleVenueTiles parse_venue(const json::JsonValue &value) {
  return {member(value, "day", rect_of), member(value, "day_lit", rect_of),
          member(value, "night", rect_of), member(value, "night_lit", rect_of)};
}

ScheduleDigits parse_digits(const json::JsonValue &value) {
  ScheduleDigits digits;
  digits.image = required(value, "image").string();
  const auto &rects = required(value, "digits").array();
  if (rects.size() != digits.digits.size()) {
    throw std::runtime_error("digits needs ten rects");
  }
  for (std::size_t index = 0; index < rects.size(); ++index) {
    digits.digits[index] = rect_of(rects[index]);
  }
  digits.tens = member(value, "tens", point_of);
  digits.units = member(value, "units", point_of);
  digits.layer = static_cast<int>(value.number_or("layer", digits.layer));
  return digits;
}

} // namespace

ScheduleScript parse_schedule(const json::JsonValue &document) {
  try {
    ScheduleScript script;
    parse_town_basics(document, script);
    const auto &buttons = required(document, "buttons");
    script.left = buttons_from_json(required(buttons, "left"));
    script.right = buttons_from_json(required(buttons, "right"));
    script.up = buttons_from_json(required(buttons, "up"));
    script.down = buttons_from_json(required(buttons, "down"));
    script.page_back = buttons_from_json(required(buttons, "page_back"));
    script.page_forward = buttons_from_json(required(buttons, "page_forward"));
    const auto &sounds = required(document, "sounds");
    script.row_sound = sounds.string_or("row", "");
    script.page_sound = sounds.string_or("page", "");
    script.exit_sound = sounds.string_or("to_exit", "");
    script.close_sound = sounds.string_or("close", "");
    script.scroll_arrow = member(document, "scroll_arrow", parse_sprite);
    const auto &exit = required(document, "exit");
    script.exit = member(exit, "normal", parse_sprite);
    script.exit_selected = member(exit, "selected", parse_sprite);
    const auto &cells = required(document, "cells");
    script.cell_image = required(cells, "image").string();
    script.cell_origin = numbers<2>(required(cells, "origin"), "origin");
    script.cell_step = numbers<2>(required(cells, "step"), "step");
    script.columns = static_cast<int>(required(cells, "columns").number());
    const auto &cursor = required(document, "cursor");
    script.cursor_frame = member(cursor, "frame", parse_sprite);
    script.cursor_pulse = member(cursor, "pulse", parse_sprite);
    script.cursor_day = member(cursor, "day", parse_sprite);
    script.cursor_day_y = static_cast<float>(required(cursor, "day_y").number());
    script.tile_layer = static_cast<int>(document.number_or("tile_layer", script.tile_layer));
    for (const auto &venue : required(document, "venues").array()) {
      script.venues.push_back(parse_venue(venue));
    }
    script.closed = member(document, "closed", rect_of);
    script.closed_lit = member(document, "closed_lit", rect_of);
    script.badge_layer = static_cast<int>(document.number_or("badge_layer", script.badge_layer));
    script.badge_dim = parse_colour(required(document, "badge_dim").string());
    for (const auto &badge : required(document, "badges").array()) {
      script.badges.push_back(
          {integers(required(badge, "classes")), member(badge, "rect", rect_of)});
    }
    script.clear = member(document, "clear", parse_sprite);
    script.no_clear_mark = integers(required(document, "no_clear_mark"));
    script.hidden_once_won = integers(required(document, "hidden_once_won"));
    script.day_digits = member(document, "day_digits", parse_digits);
    script.scroll_step_seconds = required(document, "scroll_step_seconds").number();
    script.info = member(document, "info", parse_contest_info);
    script.ticker = member(document, "ticker", parse_ticker);
    if (const auto *skip = document.find("skip")) {
      script.skip = parse_day_skip(*skip);
    }
    if (script.columns <= 0 || script.scroll_step_seconds <= 0.0 ||
        script.venues.size() != schedule_venues) {
      throw std::runtime_error("needs columns, a scroll step above 0 and three venues");
    }
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("schedule.json: ") + error.what());
  }
}

} // namespace phantomcrash
