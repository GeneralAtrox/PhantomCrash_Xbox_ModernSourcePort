// Parsing of data/ui/battle_music.json.

#include "phantomcrash/battle_music_script.hpp"

#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/music_library.hpp"

#include <algorithm>
#include <stdexcept>

namespace phantomcrash {
namespace {

using json::member;
using json::numbers;
using json::required;

float number_of(const json::JsonValue &value, const char *key) {
  return static_cast<float>(required(value, key).number());
}

std::array<float, 2> point(const json::JsonValue &value, const char *key) {
  return numbers<2>(required(value, key), key);
}

std::vector<float> floats(const json::JsonValue &value) {
  std::vector<float> out;
  for (const auto &item : value.array()) {
    out.push_back(static_cast<float>(item.number()));
  }
  return out;
}

MusicTelevision parse_television(const json::JsonValue &value) {
  MusicTelevision tv;
  tv.frame = member(value, "frame", parse_sprite);
  tv.static_frames = member(value, "static", parse_sprites);
  tv.static_period = required(value, "static_period").number();
  tv.roll = member(value, "roll", parse_sprite);
  tv.roll_distance = number_of(value, "roll_distance");
  tv.roll_seconds = required(value, "roll_seconds").number();
  tv.bars = member(value, "bars", parse_sprites);
  tv.bars_seconds = required(value, "bars_seconds").number();
  tv.bars_cycle = required(value, "bars_cycle").number();
  tv.caption_at = point(value, "caption_at");
  tv.caption_rise = number_of(value, "caption_rise");
  tv.caption_seconds = required(value, "caption_seconds").number();
  if (tv.static_frames.empty() || tv.bars.empty() || tv.static_period <= 0.0 ||
      tv.bars_seconds <= 0.0 || tv.bars_cycle <= 0.0 || tv.roll_seconds <= 0.0 ||
      tv.caption_seconds <= 0.0) {
    throw std::runtime_error("the television needs static, bars and timings above 0");
  }
  return tv;
}

MusicDiscs parse_discs(const json::JsonValue &value) {
  MusicDiscs discs;
  discs.disc = member(value, "disc", parse_sprite);
  discs.centre = number_of(value, "centre");
  discs.step = number_of(value, "step");
  discs.y = number_of(value, "y");
  discs.fan_seconds = required(value, "fan_seconds").number();
  discs.number_stagger = required(value, "number_stagger").number();
  discs.number_offset = point(value, "number_offset");
  discs.number = member(value, "number", parse_number_style);
  discs.first_layer = static_cast<int>(required(value, "first_layer").number());
  discs.tray = member(value, "tray", parse_sprite);
  discs.tray_seconds = required(value, "tray_seconds").number();
  discs.tray_from = point(value, "tray_from");
  discs.label_seconds = required(value, "label_seconds").number();
  discs.label_from = point(value, "label_from");
  discs.name_centre = point(value, "name_centre");
  discs.name_colour = parse_colour(required(value, "name_colour").string());
  return discs;
}

MusicStock parse_stock(const json::JsonValue &value) {
  MusicStock stock;
  stock.columns = static_cast<int>(required(value, "columns").number());
  stock.rows = static_cast<int>(required(value, "rows").number());
  stock.origin = point(value, "origin");
  stock.step = point(value, "step");
  stock.slot = member(value, "slot", parse_sprite);
  stock.number_offset = point(value, "number_offset");
  stock.cursor = member(value, "cursor", parse_sprite);
  stock.fresh = member(value, "fresh", parse_sprite);
  stock.fresh_offset = point(value, "fresh_offset");
  stock.ok = member(value, "ok", parse_sprite);
  stock.ok_lit = member(value, "ok_lit", parse_sprite);
  stock.portrait_frame = member(value, "portrait_frame", parse_sprite);
  stock.portrait_at = point(value, "portrait_at");
  stock.arrows = member(value, "arrows", parse_sprites);
  stock.hints = member(value, "hints", parse_sprites);
  if (value.find("heading") != nullptr) {
    stock.heading = member(value, "heading", parse_sprite);
    stock.heading_seconds = number_of(value, "heading_seconds");
    stock.heading_grow = std::max(1.0e-6, static_cast<double>(number_of(value, "heading_grow")));
    stock.heading_from = point(value, "heading_from");
  }
  if (stock.columns <= 0 || stock.rows <= 0 || stock.arrows.size() != 2) {
    throw std::runtime_error("the stock needs a grid and two arrows");
  }
  return stock;
}

} // namespace

BattleMusicScript parse_battle_music(const json::JsonValue &document) {
  try {
    BattleMusicScript script;
    parse_town_basics(document, script);
    const auto &buttons = required(document, "buttons");
    script.up = buttons_from_json(required(buttons, "up"));
    script.down = buttons_from_json(required(buttons, "down"));
    script.left = buttons_from_json(required(buttons, "left"));
    script.right = buttons_from_json(required(buttons, "right"));
    script.page_back = buttons_from_json(required(buttons, "page_back"));
    script.page_forward = buttons_from_json(required(buttons, "page_forward"));
    script.preview = buttons_from_json(required(buttons, "preview"));
    script.back_space = buttons_from_json(required(buttons, "back_space"));
    const auto &sounds = required(document, "sounds");
    script.jump_sound = sounds.string_or("jump", "");
    script.cancel_sound = sounds.string_or("cancel", "");
    script.add_sound = sounds.string_or("add", "");
    script.remove_sound = sounds.string_or("remove", "");
    script.help_style = member(document, "help", parse_ticker);
    script.help.text = required(document, "help_text").string();
    script.help.trail = number_of(document, "help_trail");
    script.track_format = required(document, "track_help").string();
    script.track_trail = number_of(document, "track_help_trail");

    const auto &menu = required(document, "menu");
    script.menu_panel = member(menu, "panel", parse_sprite);
    for (const auto &item : required(menu, "items").array()) {
      script.items.push_back(item.string());
    }
    script.exit = member(menu, "exit", parse_sprite);
    script.exit_lit = member(menu, "exit_lit", parse_sprite);
    script.menu_cursor = member(menu, "cursor", parse_sprite);
    script.menu_sweep = member(menu, "sweep", parse_sprite);
    script.item_y = floats(required(menu, "item_y"));
    script.arrive = floats(required(menu, "arrive"));
    script.slide_seconds = required(menu, "slide_seconds").number();
    script.slide_distance = number_of(menu, "slide_distance");
    script.television = member(document, "television", parse_television);
    for (const auto &[name, caption] : required(document, "captions").object()) {
      script.captions[name] = member(required(document, "captions"), name.c_str(), parse_sprite);
    }
    if (script.item_y.size() != script.items.size()) {
      throw std::runtime_error("the menu needs a place per item");
    }

    const auto &programs = required(document, "programs");
    script.program_panel = member(programs, "panel", parse_sprite);
    script.program_lit = member(programs, "lit", parse_sprites);
    script.program_cursor = member(programs, "cursor", parse_sprite);
    script.program_cursor_y = floats(required(programs, "cursor_y"));
    script.set_prompt = member(programs, "set_prompt", parse_sprite);
    script.set_ok = member(programs, "set_ok", parse_sprite);
    script.back_hints = member(programs, "back_hints", parse_sprites);
    constexpr auto programs_count = static_cast<std::size_t>(music_program_count);
    if (script.program_lit.size() != programs_count ||
        script.program_cursor_y.size() != programs_count) {
      throw std::runtime_error("programs needs a label and a cursor place per program");
    }

    const auto &mode = required(document, "play_mode");
    script.mode_panel = member(mode, "panel", parse_sprite);
    script.mode_ring = member(mode, "ring", parse_sprite);
    script.mode_step = number_of(mode, "step");
    script.cancel_hints = member(mode, "hints", parse_sprites);

    script.discs = member(document, "discs", parse_discs);
    script.stock = member(document, "stock", parse_stock);
    script.name_entry = required(document, "name_entry").string();
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("battle_music.json: ") + error.what());
  }
}

} // namespace phantomcrash
