// Parsing of data/ui/assemble.json.

#include "phantomcrash/assemble_script.hpp"

#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/menu_screen.hpp"

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

int integer_of(const json::JsonValue &value, const char *key) {
  return static_cast<int>(required(value, key).number());
}

std::uint32_t colour_of(const json::JsonValue &value, const char *key) {
  return parse_colour(required(value, key).string());
}

template <std::size_t N> std::array<Sprite, N> sprite_array(const json::JsonValue &value) {
  const auto sprites = parse_sprites(value);
  if (sprites.size() != N) {
    throw std::runtime_error("needs " + std::to_string(N) + " sprites");
  }
  std::array<Sprite, N> out;
  std::copy(sprites.begin(), sprites.end(), out.begin());
  return out;
}

AssembleItem item_named(const std::string &name) {
  static const std::map<std::string, AssembleItem> items{
      {"body", AssembleItem::body},   {"legs", AssembleItem::legs},
      {"arm", AssembleItem::arm},     {"back", AssembleItem::back},
      {"chip", AssembleItem::chip},   {"module", AssembleItem::module},
      {"paint", AssembleItem::paint}, {"sticker", AssembleItem::sticker},
      {"name", AssembleItem::name},   {"dismantle", AssembleItem::dismantle},
      {"spec", AssembleItem::spec},   {"exit", AssembleItem::exit},
  };
  const auto found = items.find(name);
  if (found == items.end()) {
    throw std::runtime_error("no ASSEMBLE item " + name);
  }
  return found->second;
}

AssembleSides parse_sides(const json::JsonValue &value) {
  AssembleSides sides;
  sides.plain = member(value, "plain", sprite_array<2>);
  sides.lit = member(value, "lit", sprite_array<2>);
  sides.rise = number_of(value, "rise");
  sides.spread = number_of(value, "spread");
  sides.seconds = required(value, "seconds").number();
  const auto &help = required(value, "help").array();
  if (help.size() != 2 || sides.seconds <= 0.0) {
    throw std::runtime_error("sides: needs a time and two help lines");
  }
  sides.help = {help[0].string(), help[1].string()};
  return sides;
}

AssembleCell parse_cell(const json::JsonValue &value) {
  AssembleCell cell;
  cell.item = item_named(required(value, "id").string());
  cell.icon = member(value, "icon", parse_sprite);
  cell.from_x = number_of(value, "from_x");
  cell.lit = member(value, "lit", parse_sprite);
  cell.help = required(value, "help").string();
  if (const json::JsonValue *kind = value.find("kind")) {
    cell.kind = goods_kind_named(kind->string());
    if (!cell.kind) {
      throw std::runtime_error("no goods kind " + kind->string());
    }
    cell.titles = member(value, "titles", parse_sprites);
    cell.labels = member(value, "labels", parse_sprites);
    cell.stats_right = number_of(value, "stats_right");
    for (const json::JsonValue &row : required(value, "totals").array()) {
      cell.totals.push_back(total_row_named(row.string()));
    }
    cell.totals_top = number_of(value, "totals_top");
    cell.totals_right = number_of(value, "totals_right");
    if (cell.titles.empty() || cell.labels.empty()) {
      throw std::runtime_error("a list's cell needs a title and labels");
    }
  }
  if (value.find("sides") != nullptr) {
    cell.sides = member(value, "sides", parse_sides);
    if (cell.titles.size() != 2) {
      throw std::runtime_error("a cell with sides needs a title a side");
    }
  }
  return cell;
}

void parse_grid(const json::JsonValue &value, AssembleScript &script) {
  script.columns = integer_of(value, "columns");
  script.grid_rows = integer_of(value, "rows");
  script.slide_seconds = required(value, "slide_seconds").number();
  for (const json::JsonValue &cell : required(value, "cells").array()) {
    script.cells.push_back(parse_cell(cell));
  }
  script.disabled = member(value, "disabled", parse_sprite);
  if (script.columns <= 0 || script.grid_rows <= 0 ||
      static_cast<int>(script.cells.size()) != script.columns * script.grid_rows) {
    throw std::runtime_error("grid: needs columns times rows cells");
  }
}

AssemblePanel parse_panel(const json::JsonValue &value) {
  AssemblePanel panel;
  panel.numbers = member(value, "numbers", parse_sprites);
  panel.sockets_used = numbers<2>(required(value, "sockets_used"), "sockets_used");
  panel.sockets_total = numbers<2>(required(value, "sockets_total"), "sockets_total");
  panel.sockets_colour = colour_of(value, "sockets_colour");
  panel.sockets_layer = integer_of(value, "sockets_layer");
  const auto &name = required(value, "name");
  panel.name_at = numbers<2>(required(name, "position"), "position");
  panel.name_colour = colour_of(name, "colour");
  panel.name_layer = integer_of(name, "layer");
  panel.name_glyphs = static_cast<int>(name.number_or("max_glyphs", 9));
  panel.no_equip = member(value, "no_equip", sprite_array<3>);
  panel.chip_at = numbers<2>(required(value, "chip_at"), "chip_at");
  panel.paint_at = numbers<2>(required(value, "paint_at"), "paint_at");
  panel.emblem_at = numbers<2>(required(value, "emblem_at"), "emblem_at");
  panel.equip_layer = integer_of(value, "equip_layer");
  panel.incomplete = member(value, "incomplete", parse_sprite);
  panel.underkill = member(value, "underkill", parse_sprite);
  panel.overweight = member(value, "overweight", parse_sprite);
  panel.pair_x = numbers<2>(required(value, "pair_x"), "pair_x");
  return panel;
}

AssembleTotalsStyle parse_totals(const json::JsonValue &value) {
  AssembleTotalsStyle totals;
  totals.step = number_of(value, "step");
  totals.layer = integer_of(value, "layer");
  totals.colour = colour_of(value, "colour");
  totals.up = member(value, "up", parse_sprite);
  totals.down = member(value, "down", parse_sprite);
  totals.arrow_dx = number_of(value, "arrow_dx");
  totals.arrow_dy = number_of(value, "arrow_dy");
  totals.pair_gap = number_of(value, "pair_gap");
  totals.chip_name_at = numbers<2>(required(value, "chip_name_at"), "chip_name_at");
  if (value.find("negative") != nullptr) {
    totals.negative = member(value, "negative", parse_sprite);
    totals.negative_dx = number_of(value, "negative_dx");
    totals.negative_dy = number_of(value, "negative_dy");
  }
  if (value.find("up_colour") != nullptr) {
    totals.up_colour = colour_of(value, "up_colour");
  }
  if (value.find("down_colour") != nullptr) {
    totals.down_colour = colour_of(value, "down_colour");
  }
  return totals;
}

AssembleDetails parse_details(const json::JsonValue &value) {
  AssembleDetails details;
  details.box = member(value, "box", parse_sprite);
  details.box_alpha = number_of(value, "box_alpha");
  details.fade_seconds = required(value, "fade_seconds").number();
  details.equipped = member(value, "equipped", parse_sprite);
  details.totals = member(value, "totals", parse_totals);
  details.picture_at = numbers<2>(required(value, "picture_at"), "picture_at");
  details.picture_layer = integer_of(value, "picture_layer");
  if (const json::JsonValue *extras = value.find("extra_stats")) {
    details.extra_stats = parse_extra_stats_style(*extras);
    details.extra_title_rows = number_of(*extras, "title_rows");
    details.extra_rows_top = number_of(*extras, "rows_top");
  }
  if (details.fade_seconds <= 0.0) {
    throw std::runtime_error("details: fade_seconds must be above 0");
  }
  return details;
}

void parse_sounds(const json::JsonValue &sounds, AssembleScript &script) {
  script.row_sound = sounds.string_or("row", "");
  script.back_sound = sounds.string_or("back", "");
  script.exit_sound = sounds.string_or("exit", "");
  script.list_row_sound = sounds.string_or("list_row", "");
  script.sort_sound = sounds.string_or("sort", "");
  script.fit_sound = sounds.string_or("fit", "");
  script.question_sound = sounds.string_or("question", "");
  script.dismantle_sound = sounds.string_or("dismantle", "");
  script.no_sound = sounds.string_or("no", "");
  script.hide_sound = sounds.string_or("hide", "");
}

void parse_list(const json::JsonValue &document, AssembleScript &script) {
  const auto &list = required(document, "list");
  script.list = member(document, "list", parse_goods_list);
  script.list.stats = member(document, "stats", parse_part_stats_style);
  script.unusable_colour = colour_of(list, "unusable_colour");
  script.fitted_colour = colour_of(list, "fitted_colour");
  script.badge_colour = colour_of(list, "badge_colour");
}

void parse_texts(const json::JsonValue &document, AssembleScript &script) {
  script.message_text_at = numbers<2>(required(document, "message_text_at"), "message_text_at");
  for (const auto &[id, lines] : required(document, "messages").object()) {
    script.messages[id] = parse_lines(lines);
  }
  script.help_style = member(document, "help", parse_ticker);
  const auto &help = required(document, "help_text");
  script.list_help = required(help, "list").string();
  script.details_help = required(help, "details").string();
  script.hide_on_help = required(help, "hide_on").string();
  script.hide_off_help = required(help, "hide_off").string();
}

} // namespace

const AssembleCell *AssembleScript::cell_for(const AssembleItem item) const {
  const auto found = std::ranges::find(cells, item, &AssembleCell::item);
  return found != cells.end() ? &*found : nullptr;
}

AssembleScript parse_assemble(const json::JsonValue &document) {
  try {
    AssembleScript script;
    parse_town_basics(document, script);
    const auto &buttons = required(document, "buttons");
    script.left = buttons_from_json(required(buttons, "left"));
    script.right = buttons_from_json(required(buttons, "right"));
    script.up = buttons_from_json(required(buttons, "up"));
    script.down = buttons_from_json(required(buttons, "down"));
    script.sort_name = buttons_from_json(required(buttons, "sort_name"));
    script.sort_price = buttons_from_json(required(buttons, "sort_price"));
    script.hide_incompatible = buttons_from_json(required(buttons, "hide_incompatible"));
    script.page_back = buttons_from_json(required(buttons, "page_back"));
    script.page_next = buttons_from_json(required(buttons, "page_next"));
    script.page_top = buttons_from_json(required(buttons, "page_top"));
    script.page_bottom = buttons_from_json(required(buttons, "page_bottom"));
    parse_sounds(required(document, "sounds"), script);
    parse_grid(required(document, "grid"), script);
    script.panel = member(document, "panel", parse_panel);
    parse_list(document, script);
    script.details = member(document, "details", parse_details);
    for (const auto &[name, screen] : required(document, "routes").object()) {
      script.routes[item_named(name)] = screen.string();
    }
    script.same_maker = document.boolean_or("same_maker", true);
    for (const json::JsonValue &maker : required(document, "unpaintable_makers").array()) {
      script.unpaintable_makers.push_back(static_cast<int>(maker.number()));
    }
    script.overweight_margin = number_of(document, "overweight_margin");
    parse_texts(document, script);
    if (script.cell_for(AssembleItem::exit) == nullptr) {
      throw std::runtime_error("grid: needs an EXIT");
    }
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("assemble.json: ") + error.what());
  }
}

} // namespace phantomcrash
