// Parsing of data/ui/records.json.

#include "phantomcrash/records_script.hpp"

#include "phantomcrash/json_fields.hpp"

#include <stdexcept>

namespace phantomcrash {
namespace {

using json::member;
using json::numbers;
using json::required;

float number_of(const json::JsonValue &value, const char *key) {
  return static_cast<float>(required(value, key).number());
}

std::vector<int> integers(const json::JsonValue &value) {
  std::vector<int> out;
  for (const auto &item : value.array()) {
    out.push_back(static_cast<int>(item.number()));
  }
  return out;
}

std::vector<std::array<float, 2>> points(const json::JsonValue &value) {
  std::vector<std::array<float, 2>> out;
  for (const auto &item : value.array()) {
    out.push_back(numbers<2>(item, "point"));
  }
  return out;
}

RecordFormat parse_format(const std::string &name) {
  if (name == "count") {
    return RecordFormat::count;
  }
  if (name == "percent") {
    return RecordFormat::percent;
  }
  if (name == "money") {
    return RecordFormat::money;
  }
  if (name == "time") {
    return RecordFormat::time;
  }
  throw std::runtime_error("unknown format " + name);
}

RecordRow parse_row(const json::JsonValue &value) {
  RecordRow row;
  row.label = required(value, "label").string();
  row.stat = value.string_or("stat", "");
  if (const auto *ratio = value.find("ratio")) {
    const auto &pair = ratio->array();
    if (pair.size() != 2) {
      throw std::runtime_error("ratio needs two stats");
    }
    row.stat = pair[0].string();
    row.ratio_of = pair[1].string();
  }
  row.completion = value.boolean_or("completion", false);
  row.format = parse_format(value.string_or("format", "count"));
  if (row.stat.empty() && !row.completion) {
    throw std::runtime_error("row " + row.label + " needs a stat");
  }
  return row;
}

RecordPage parse_page(const json::JsonValue &value) {
  RecordPage page;
  page.panel = member(value, "panel", parse_sprite);
  page.growth = {required(value, "grow_seconds").number(), 0.0F};
  page.hint = member(value, "hint", parse_sprites);
  page.label_x = number_of(value, "label_x");
  page.first_y = number_of(value, "first_y");
  page.step_y = number_of(value, "step_y");
  for (const auto &row : required(value, "rows").array()) {
    page.rows.push_back(parse_row(row));
  }
  return page;
}

RowReveal parse_reveal(const json::JsonValue &value) {
  RowReveal reveal;
  reveal.first_seconds = required(value, "first_seconds").number();
  reveal.step_seconds = required(value, "step_seconds").number();
  reveal.fade_seconds = required(value, "fade_seconds").number();
  reveal.whiten_seconds = required(value, "whiten_seconds").number();
  reveal.start_colour = parse_colour(required(value, "start_colour").string());
  reveal.bar = member(value, "bar", parse_sprite);
  reveal.bar_x = number_of(value, "bar_x");
  reveal.bar_above = number_of(value, "bar_above");
  reveal.bar_until = number_of(value, "bar_until");
  reveal.bar_shrink_from = number_of(value, "bar_shrink_from");
  if (reveal.fade_seconds <= 0.0 || reveal.whiten_seconds <= 0.0) {
    throw std::runtime_error("the fades must be above 0");
  }
  return reveal;
}

RecordNumbers parse_numbers(const json::JsonValue &value) {
  RecordNumbers numbers_style;
  numbers_style.right = number_of(value, "right");
  numbers_style.time_left = number_of(value, "time_left");
  numbers_style.hours_max = static_cast<int>(required(value, "hours_max").number());
  numbers_style.percent = required(value, "percent").string();
  numbers_style.money = required(value, "money").string();
  return numbers_style;
}

RecordsMenu parse_menu(const json::JsonValue &value) {
  RecordsMenu menu;
  menu.panel = member(value, "panel", parse_sprite);
  for (const auto &item : required(value, "items").array()) {
    menu.items.push_back({member(item, "label", parse_sprite), required(item, "opens").string()});
  }
  menu.lit_colour = parse_colour(required(value, "lit_colour").string());
  menu.lit_layer = static_cast<int>(required(value, "lit_layer").number());
  menu.behind_alpha = number_of(value, "behind_alpha");
  const auto behind = numbers<2>(required(value, "behind_layers"), "behind_layers");
  menu.behind_panel_layer = static_cast<int>(behind[0]);
  menu.behind_items_layer = static_cast<int>(behind[1]);
  if (menu.items.empty()) {
    throw std::runtime_error("the menu needs items");
  }
  return menu;
}

ConventionVenue parse_venue(const json::JsonValue &value) {
  ConventionVenue venue;
  venue.contests = integers(required(value, "contests"));
  venue.header = member(value, "header", parse_sprite);
  if (const auto *ranks = value.find("ranks")) {
    venue.ranks = parse_sprites(*ranks);
  }
  venue.champion_names = member(value, "champion_names", parse_sprites);
  if (value.find("backdrop") != nullptr) {
    venue.backdrop = member(value, "backdrop", parse_sprite);
  }
  return venue;
}

ConventionRegulation parse_regulation(const json::JsonValue &value) {
  ConventionRegulation regulation;
  regulation.contests = parse_venue(value);
  regulation.columns = static_cast<int>(required(value, "columns").number());
  regulation.rows = static_cast<int>(required(value, "rows").number());
  regulation.grid = member(value, "grid", parse_sprite);
  const auto &own = required(value, "own_portrait");
  regulation.own_portrait_slot = static_cast<int>(required(own, "slot").number());
  regulation.own_portrait = member(own, "sprite", parse_sprite);
  regulation.arrows = member(value, "arrows", parse_sprites);
  regulation.arrows_lit = member(value, "arrows_lit", parse_sprites);
  regulation.flip_seconds = required(value, "flip_seconds").number();
  if (regulation.columns <= 0 || regulation.rows <= 0 || regulation.arrows.size() != 2 ||
      regulation.arrows_lit.size() != 2) {
    throw std::runtime_error("regulation needs a grid and two arrows");
  }
  return regulation;
}

HiddenChampion parse_hidden(const json::JsonValue &value) {
  HiddenChampion hidden;
  hidden.contest = static_cast<int>(required(value, "contest").number());
  hidden.until_event = static_cast<int>(required(value, "until_event").number());
  hidden.portraits = member(value, "portraits", parse_sprites);
  hidden.name = member(value, "name", parse_sprite);
  return hidden;
}

ConventionStyle parse_convention(const json::JsonValue &value) {
  ConventionStyle style;
  style.ellipse = member(value, "ellipse", parse_sprite);
  style.ellipse_growth = {required(value, "ellipse_seconds").number(), 0.0F};
  style.first_ranker = member(value, "first_ranker", parse_sprite);
  style.first_ranker_contest = static_cast<int>(required(value, "first_ranker_contest").number());
  style.first_ranker_portraits = points(required(value, "first_ranker_portraits"));
  style.items = member(value, "items", parse_sprites);
  for (const auto &opens : required(value, "item_opens").array()) {
    style.item_opens.push_back(opens.string());
  }
  style.dim_colour = parse_colour(required(value, "dim_colour").string());
  style.slide_seconds = required(value, "slide_seconds").number();
  style.slide_distance = number_of(value, "slide_distance");
  style.slide_stagger = number_of(value, "slide_stagger");
  style.panel = member(value, "panel", parse_sprite);
  style.growth = {required(value, "grow_seconds").number(), 0.0F};
  style.hint = member(value, "hint", parse_sprites);
  style.grid = member(value, "grid", parse_sprite);
  style.cursor = member(value, "cursor", parse_sprite);
  style.cursor_origin = numbers<2>(required(value, "cursor_origin"), "cursor_origin");
  style.cursor_step = number_of(value, "cursor_step");
  style.area_cursor = member(value, "area_cursor", parse_sprite);
  style.day_night = member(value, "day_night", parse_sprites);
  style.portraits = points(required(value, "portraits"));
  style.portrait_layer = static_cast<int>(required(value, "portrait_layer").number());
  style.loser = member(value, "loser", parse_sprite);
  style.loser_step = number_of(value, "loser_step");
  style.contest_name = numbers<2>(required(value, "contest_name"), "contest_name");
  style.text_colour = parse_colour(required(value, "text_colour").string());
  style.text_layer = static_cast<int>(required(value, "text_layer").number());
  style.hidden = member(value, "hidden", parse_hidden);
  for (const auto &venue : required(value, "venues").array()) {
    style.venues.push_back(parse_venue(venue));
  }
  style.regulation = member(value, "regulation", parse_regulation);
  if (style.items.size() != style.item_opens.size() || style.day_night.size() != 2 ||
      style.slide_seconds <= 0.0) {
    throw std::runtime_error("convention needs an opening per item, day and night");
  }
  return style;
}

Vec3 vec3(const json::JsonValue &value, const char *what) {
  const auto items = json::numbers<3>(value, what);
  return {items[0], items[1], items[2]};
}

std::vector<Vec3> vec3s(const json::JsonValue &value, const char *what) {
  std::vector<Vec3> items;
  for (const json::JsonValue &item : value.array()) {
    items.push_back(vec3(item, what));
  }
  return items;
}

RecordTrophies parse_trophies(const json::JsonValue &value) {
  RecordTrophies trophies;
  trophies.model = required(value, "model").string();
  const json::JsonValue &camera = required(value, "camera");
  trophies.eye = vec3(required(camera, "eye"), "eye");
  trophies.target = vec3(required(camera, "target"), "target");
  trophies.fov_degrees = static_cast<float>(camera.number_or("fov_degrees", trophies.fov_degrees));
  trophies.near_plane = static_cast<float>(camera.number_or("near_plane", trophies.near_plane));
  trophies.far_plane = static_cast<float>(camera.number_or("far_plane", trophies.far_plane));
  trophies.light_direction = vec3(required(value, "light_direction"), "light_direction");
  trophies.diffuse = static_cast<float>(value.number_or("diffuse", trophies.diffuse));
  trophies.ambient = static_cast<float>(value.number_or("ambient", trophies.ambient));
  trophies.frame_rate = static_cast<float>(value.number_or("frame_rate", trophies.frame_rate));
  trophies.spin_per_frame =
      static_cast<float>(value.number_or("spin_per_frame", trophies.spin_per_frame));
  trophies.layer = static_cast<int>(value.number_or("layer", trophies.layer));
  const json::JsonValue &first = required(value, "first_ranker");
  trophies.first_ranker_node = static_cast<int>(required(first, "node").number());
  trophies.first_ranker = vec3(required(first, "position"), "first_ranker position");
  trophies.first_ranker_tilt = static_cast<float>(first.number_or("tilt", 0.0));
  trophies.class_node = static_cast<int>(required(value, "class_node").number());
  trophies.class_cells = vec3s(required(value, "class_cells"), "class cell");
  trophies.area_nodes = integers(required(value, "area_nodes"));
  trophies.area_ranker = vec3(required(value, "area_ranker"), "area_ranker");
  trophies.regulation_cells = vec3s(required(value, "regulation_cells"), "regulation cell");
  trophies.regulation_nodes = integers(required(value, "regulation_nodes"));
  if (trophies.regulation_nodes.size() != trophies.regulation_cells.size()) {
    throw std::runtime_error("regulation_nodes: one for each regulation cell");
  }
  trophies.cell_step = static_cast<float>(value.number_or("cell_step", trophies.cell_step));
  trophies.won_cell = member(value, "won_cell", parse_sprite);
  trophies.area_won_cell = member(value, "area_won_cell", parse_sprite);
  trophies.no_convention = member(value, "no_convention", parse_sprite);
  return trophies;
}

} // namespace

RecordsScript parse_records(const json::JsonValue &document) {
  try {
    RecordsScript script;
    parse_town_basics(document, script);
    const auto &buttons = required(document, "buttons");
    script.up = buttons_from_json(required(buttons, "up"));
    script.down = buttons_from_json(required(buttons, "down"));
    script.left = buttons_from_json(required(buttons, "left"));
    script.right = buttons_from_json(required(buttons, "right"));
    script.page_back = buttons_from_json(required(buttons, "page_back"));
    script.page_forward = buttons_from_json(required(buttons, "page_forward"));
    const auto &sounds = required(document, "sounds");
    script.column_sound = sounds.string_or("column", "");
    script.menu_sound = sounds.string_or("menu", script.move_sound);
    script.exit_sound = sounds.string_or("to_exit", "");
    script.close_sound = sounds.string_or("close", "");
    script.row_sound = sounds.string_or("row", "");
    script.page_sound = sounds.string_or("page", "");
    script.menu = member(document, "menu", parse_menu);
    script.reveal = member(document, "reveal", parse_reveal);
    script.numbers = member(document, "number", parse_numbers);
    const auto &pages = required(document, "pages");
    for (const auto &[name, page] : pages.object()) {
      script.pages[name] = member(pages, name.c_str(), parse_page);
    }
    script.completion_contests = integers(required(document, "completion_contests"));
    if (document.find("help") != nullptr) {
      script.help = member(document, "help", parse_ticker);
      script.help_text = required(document, "help_text").string();
    }
    if (document.find("trophies") != nullptr) {
      script.trophies = member(document, "trophies", parse_trophies);
    }
    if (const auto *unscored = document.find("unscored_events")) {
      script.unscored_events = integers(*unscored);
    }
    script.convention = member(document, "convention", parse_convention);
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("records.json: ") + error.what());
  }
}

} // namespace phantomcrash
