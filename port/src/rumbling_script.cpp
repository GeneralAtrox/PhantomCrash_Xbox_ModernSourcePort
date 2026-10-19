// Parsing of data/ui/rumbling.json.

#include "phantomcrash/rumbling_script.hpp"

#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/menu_screen.hpp"

#include <stdexcept>

namespace phantomcrash {
namespace {

using json::member;
using json::numbers;
using json::required;

float number_of(const json::JsonValue &value, const char *key) {
  return static_cast<float>(required(value, key).number());
}

std::uint32_t colour_of(const json::JsonValue &value, const char *key) {
  return parse_colour(required(value, key).string());
}

std::vector<CanvasPoint> points_of(const json::JsonValue &value, const char *key) {
  std::vector<CanvasPoint> points;
  if (const json::JsonValue *list = value.find(key)) {
    for (const json::JsonValue &point : list->array()) {
      points.push_back(numbers<2>(point, key));
    }
  }
  return points;
}

std::vector<std::string> lines_of(const json::JsonValue &value) {
  std::vector<std::string> lines;
  for (const json::JsonValue &line : value.array()) {
    lines.push_back(line.string());
  }
  return lines;
}

RumblingArea parse_area(const json::JsonValue &value) {
  RumblingArea area;
  area.id = required(value, "id").string();
  area.help = required(value, "help").string();
  area.exit = value.boolean_or("exit", false);
  area.picture = member(value, "picture", parse_sprite);
  area.night_picture = member(value, "night_picture", parse_sprite);
  area.label = member(value, "label", parse_sprite);
  area.title = member(value, "title", parse_sprite);
  area.item = member(value, "item", parse_sprite);
  area.item_lit = member(value, "item_lit", parse_sprite);
  area.item_from_x = number_of(value, "item_from_x");
  if (value.find("map_frames") != nullptr) {
    area.map_frames = member(value, "map_frames", parse_sprites);
  }
  area.gates = points_of(value, "gates");
  area.marks = points_of(value, "marks");
  if (const json::JsonValue *map = value.find("robot_map")) {
    area.robot_map = parse_rumbling_map_projection(*map);
  }
  return area;
}

RumblingAreaSelect parse_area_select(const json::JsonValue &value) {
  RumblingAreaSelect style;
  style.slide_seconds = required(value, "slide_seconds").number();
  style.bar = member(value, "bar", parse_sprite);
  style.bar_from_x = number_of(value, "bar_from_x");
  style.bar_slide_seconds = required(value, "bar_slide_seconds").number();
  style.picture_frame = member(value, "picture_frame", parse_sprite);
  style.fade_seconds = required(value, "fade_seconds").number();
  style.closed = member(value, "closed", parse_sprite);
  style.noise = member(value, "noise", parse_sprite);
  style.noise_alpha = static_cast<float>(value.number_or("noise_alpha", style.noise_alpha));
  style.info_panel = member(value, "info_panel", parse_sprite);
  style.info_right = number_of(value, "info_right");
  style.contest_y = number_of(value, "contest_y");
  style.class_y = number_of(value, "class_y");
  style.fee_at = {number_of(value, "fee_right"), number_of(value, "fee_y")};
  style.fee_digits = static_cast<int>(value.number_or("fee_digits", style.fee_digits));
  style.text_colour = colour_of(value, "text_colour");
  style.text_layer = static_cast<int>(required(value, "text_layer").number());
  style.clear = member(value, "clear", parse_sprite);
  for (const json::JsonValue &contest : required(value, "no_clear_mark").array()) {
    style.no_clear_mark.push_back(static_cast<int>(contest.number()));
  }
  return style;
}

RumblingAction action_named(const std::string &name) {
  static const std::map<std::string, RumblingAction> actions{
      {"entry", RumblingAction::entry},
      {"rental", RumblingAction::rental},
      {"select_sv", RumblingAction::select_sv},
      {"character_info", RumblingAction::character_info},
      {"contest_info", RumblingAction::contest_info},
      {"back", RumblingAction::back},
  };
  const auto found = actions.find(name);
  if (found == actions.end()) {
    throw std::runtime_error("no sub-menu action is named " + name);
  }
  return found->second;
}

RumblingMenu parse_menu(const json::JsonValue &value) {
  RumblingMenu menu;
  menu.turn_seconds = required(value, "turn_seconds").number();
  menu.info_slide = number_of(value, "info_slide");
  menu.title_at = numbers<2>(required(value, "title_at"), "title_at");
  for (const json::JsonValue &item : required(value, "items").array()) {
    menu.items.push_back({action_named(required(item, "action").string()),
                          required(item, "help").string(), member(item, "normal", parse_sprite),
                          member(item, "lit", parse_sprite)});
  }
  menu.disabled_colour = colour_of(value, "disabled_colour");
  menu.item_slide = number_of(value, "item_slide");
  menu.map = member(value, "map", parse_sprite);
  menu.map_frame_seconds = required(value, "map_frame_seconds").number();
  menu.map_fade_colour = colour_of(value, "map_fade_colour");
  menu.mark = member(value, "mark", parse_sprite);
  return menu;
}

RumblingGates parse_gates(const json::JsonValue &value) {
  RumblingGates gates;
  gates.window = member(value, "window", parse_sprite);
  gates.hint = member(value, "hint", parse_sprites);
  gates.labels = member(value, "labels", parse_sprites);
  for (const auto &[count, x] : required(value, "label_x").object()) {
    gates.label_x[std::stoi(count)] = static_cast<float>(x.number());
  }
  gates.label_step = number_of(value, "label_step");
  gates.chosen_colour = colour_of(value, "chosen_colour");
  gates.chosen_layer = static_cast<int>(required(value, "chosen_layer").number());
  gates.marker = member(value, "marker", parse_sprite);
  gates.marker_lit = member(value, "marker_lit", parse_sprite);
  gates.ring = member(value, "ring", parse_sprite);
  gates.ring_degrees_per_second = number_of(value, "ring_degrees_per_second");
  gates.help = required(value, "help").string();
  return gates;
}

RumblingContestInfo parse_info(const json::JsonValue &value) {
  RumblingContestInfo info;
  info.window = member(value, "window", parse_pop_window);
  info.style = parse_contest_info(value);
  info.close_hint = member(value, "close_hint", parse_sprites);
  info.help = required(value, "help").string();
  return info;
}

RumblingCharacterInfo parse_character_info(const json::JsonValue &value) {
  RumblingCharacterInfo info;
  info.window = member(value, "window", parse_pop_window);
  info.pilot = numbers<2>(required(value, "pilot"), "pilot");
  info.robot = numbers<2>(required(value, "robot"), "robot");
  info.partner = numbers<2>(required(value, "partner"), "partner");
  info.team = numbers<2>(required(value, "team"), "team");
  info.bounty = numbers<2>(required(value, "bounty"), "bounty");
  info.bounty_digits = static_cast<int>(value.number_or("bounty_digits", info.bounty_digits));
  info.bounty_unit = member(value, "bounty_unit", parse_sprite);
  info.no_team = value.string_or("no_team", "");
  info.text_colour = colour_of(value, "text_colour");
  info.text_layer = static_cast<int>(number_of(value, "text_layer"));
  info.pilot_face = numbers<2>(required(value, "pilot_face"), "pilot_face");
  info.partner_face = numbers<2>(required(value, "partner_face"), "partner_face");
  info.face_layer = static_cast<int>(number_of(value, "face_layer"));
  info.close_hint = member(value, "close_hint", parse_sprites);
  if (const json::JsonValue *armour = value.find("armour")) {
    info.show_armour = true;
    info.armour = numbers<2>(required(*armour, "at"), "at");
    info.armour_label = armour->string_or("label", "");
    info.down_label = armour->string_or("down", "");
  }
  return info;
}

void parse_sounds(const json::JsonValue &sounds, RumblingScript &script) {
  script.row_sound = sounds.string_or("row", "");
  script.to_exit_sound = sounds.string_or("to_exit", "");
  script.no_sound = sounds.string_or("no", "");
  script.close_sound = sounds.string_or("close", "");
  script.browse_sound = sounds.string_or("browse", "");
}

} // namespace

RumblingScript parse_rumbling(const json::JsonValue &document) {
  try {
    RumblingScript script;
    parse_town_basics(document, script);
    const auto &buttons = required(document, "buttons");
    script.left = buttons_from_json(required(buttons, "left"));
    script.right = buttons_from_json(required(buttons, "right"));
    script.up = buttons_from_json(required(buttons, "up"));
    script.down = buttons_from_json(required(buttons, "down"));
    parse_sounds(required(document, "sounds"), script);
    const auto &routes = required(document, "routes");
    script.hangar_route = routes.string_or("select_sv", script.hangar_route);
    script.battle_route = routes.string_or("battle", script.battle_route);
    for (const json::JsonValue &area : required(document, "areas").array()) {
      script.areas.push_back(parse_area(area));
    }
    if (script.areas.empty() || !script.areas.back().exit) {
      throw std::runtime_error("areas: the last one is the way out");
    }
    script.area_select = member(document, "area_select", parse_area_select);
    script.menu = member(document, "menu", parse_menu);
    script.gates = member(document, "gates", parse_gates);
    if (document.find("robot_markers") != nullptr) {
      script.robot_markers = member(document, "robot_markers", parse_rumbling_robot_markers);
    }
    script.character_info = member(document, "character_info", parse_character_info);
    script.contest_info = member(document, "contest_info", parse_info);
    script.message_text_at = numbers<2>(required(document, "message_text_at"), "message_text_at");
    for (const auto &[id, lines] : required(document, "messages").object()) {
      script.messages[id] = lines_of(lines);
    }
    script.help_style = member(document, "help", parse_ticker);
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("rumbling.json: ") + error.what());
  }
}

} // namespace phantomcrash
