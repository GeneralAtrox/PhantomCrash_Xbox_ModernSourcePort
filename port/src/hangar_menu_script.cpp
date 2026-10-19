// Parsing of data/ui/hangar.json.

#include "phantomcrash/hangar_menu_script.hpp"

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

int integer_of(const json::JsonValue &value, const char *key) {
  return static_cast<int>(required(value, key).number());
}

std::uint32_t colour_of(const json::JsonValue &value, const char *key) {
  return parse_colour(required(value, key).string());
}

std::vector<float> floats(const json::JsonValue &value) {
  std::vector<float> out;
  for (const json::JsonValue &item : value.array()) {
    out.push_back(static_cast<float>(item.number()));
  }
  return out;
}

HangarRobots parse_robots(const json::JsonValue &value) {
  HangarRobots robots;
  robots.spacing = number_of(value, "spacing");
  robots.yaw_per_step = number_of(value, "yaw_per_step");
  robots.neighbours = static_cast<int>(required(value, "neighbours").number());
  if (robots.neighbours < 0) {
    throw std::runtime_error("robots.neighbours cannot be negative");
  }
  return robots;
}

HangarCards parse_cards(const json::JsonValue &value) {
  HangarCards cards;
  cards.centre_x = number_of(value, "centre_x");
  cards.y = number_of(value, "y");
  cards.step = number_of(value, "step");
  cards.reach = number_of(value, "reach");
  cards.period = number_of(value, "period");
  cards.scroll_seconds = required(value, "scroll_seconds").number();
  cards.card = member(value, "card", parse_sprite);
  cards.colour = colour_of(value, "colour");
  cards.riding_colour = colour_of(value, "riding_colour");
  cards.numbers = member(value, "numbers", parse_hangar_labels);
  cards.for_sale = member(value, "for_sale", parse_sprite);
  cards.price = member(value, "price", parse_number_style);
  cards.price_layer = integer_of(value, "price_layer");
  cards.empty = member(value, "empty", parse_sprite);
  cards.ride = member(value, "ride", parse_sprite);
  cards.emblems = member(value, "emblems", parse_sprites);
  const auto &name = required(value, "name");
  cards.name_at = numbers<2>(required(name, "position"), "position");
  cards.name_colour = colour_of(name, "colour");
  cards.name_layer = integer_of(name, "layer");
  cards.name_glyphs = static_cast<int>(name.number_or("max_glyphs", 9));
  cards.name_line = member(value, "name_line", parse_sprite);
  cards.incomplete = member(value, "incomplete", parse_sprite);
  cards.underkill = member(value, "underkill", parse_sprite);
  cards.overweight = member(value, "overweight", parse_sprite);
  cards.warning_step = number_of(value, "warning_step");
  cards.exit = member(value, "exit", parse_sprite);
  if (cards.period <= 0.0F || cards.scroll_seconds <= 0.0 ||
      cards.numbers.size() != garage_hangar_count) {
    throw std::runtime_error("cards: needs a period, a scroll time and one number per hangar");
  }
  return cards;
}

HangarPlates parse_plates(const json::JsonValue &value) {
  HangarPlates plates;
  plates.plates = member(value, "plates", parse_hangar_labels);
  plates.marks = member(value, "marks", parse_hangar_labels);
  plates.from_x = floats(required(value, "from_x"));
  plates.to_x = floats(required(value, "to_x"));
  plates.y = number_of(value, "y");
  plates.slide_seconds = required(value, "slide_seconds").number();
  plates.riding_colour = colour_of(value, "riding_colour");
  plates.chosen_colour = colour_of(value, "chosen_colour");
  plates.chosen_layer = integer_of(value, "chosen_layer");
  plates.line = member(value, "line", parse_sprite);
  plates.exit = member(value, "exit", parse_sprite);
  plates.exit_lit = member(value, "exit_lit", parse_sprite);
  const std::size_t count = garage_hangar_count;
  if (plates.plates.size() != count || plates.marks.size() != count ||
      plates.from_x.size() != count || plates.to_x.size() != count || plates.slide_seconds <= 0.0) {
    throw std::runtime_error("plates: needs one plate, mark and place per hangar and a slide time");
  }
  return plates;
}

HangarAction action_named(const std::string &name) {
  static const std::map<std::string, HangarAction> actions{
      {"ride", HangarAction::ride},   {"test_drive", HangarAction::test_drive},
      {"spec", HangarAction::spec},   {"assemble", HangarAction::assemble},
      {"close", HangarAction::close}, {"keep", HangarAction::keep},
  };
  const auto found = actions.find(name);
  if (found == actions.end()) {
    throw std::runtime_error("no hangar action " + name);
  }
  return found->second;
}

// A menu's window and items; an origin's menu keeps the rest of `base`.
HangarMenuStyle parse_menu_items(const json::JsonValue &value, HangarMenuStyle menu) {
  menu.window = member(value, "window", parse_pop_window);
  menu.items.clear();
  for (const json::JsonValue &item : required(value, "items").array()) {
    menu.items.push_back({action_named(required(item, "id").string()),
                          member(item, "sprite", parse_sprite), required(item, "help").string()});
  }
  if (menu.items.empty() || menu.items.back().action != HangarAction::close) {
    throw std::runtime_error("menu: needs CLOSE last");
  }
  return menu;
}

HangarMenuStyle parse_menu(const json::JsonValue &value) {
  HangarMenuStyle menu;
  menu.lit_colour = colour_of(value, "lit_colour");
  menu.lit_layer = integer_of(value, "lit_layer");
  menu.strike = member(value, "strike", parse_sprite);
  menu.strike_alpha = static_cast<float>(value.number_or("strike_alpha", 1.0));
  return parse_menu_items(value, std::move(menu));
}

HangarOrigin parse_origin(const json::JsonValue &value, const HangarMenuStyle &base) {
  HangarOrigin origin;
  if (value.find("exit") != nullptr) {
    origin.exit = member(value, "exit", parse_sprite);
  }
  if (const json::JsonValue *help = value.find("exit_help")) {
    origin.exit_help = help->string();
  }
  if (const json::JsonValue *refuse = value.find("refuse")) {
    origin.refuse = HangarRefusals{required(*refuse, "incomplete").string(),
                                   required(*refuse, "unarmed").string()};
  }
  if (const json::JsonValue *menu = value.find("menu")) {
    origin.menu = parse_menu_items(*menu, base);
  }
  return origin;
}

void parse_sounds(const json::JsonValue &sounds, HangarMenuScript &script) {
  script.turn_sound = sounds.string_or("turn", "");
  script.row_sound = sounds.string_or("row", "");
  script.back_sound = sounds.string_or("back", "");
  script.exit_sound = sounds.string_or("exit", "");
  script.close_sound = sounds.string_or("close", "");
  script.no_sound = sounds.string_or("no", "");
  script.trade_sound = sounds.string_or("trade", "");
  script.board_sound = sounds.string_or("board", "");
  if (const json::JsonValue *notices = sounds.find("board_notices")) {
    for (const json::JsonValue &notice : notices->array()) {
      script.board_notices.push_back(notice.string());
    }
  }
}

void parse_texts(const json::JsonValue &document, HangarMenuScript &script) {
  script.message_text_at = numbers<2>(required(document, "message_text_at"), "message_text_at");
  for (const auto &[id, lines] : required(document, "messages").object()) {
    script.messages[id] = parse_lines(lines);
  }
  script.help_style = member(document, "help", parse_ticker);
  const auto &help = required(document, "help_text");
  script.hangar_help = required(help, "hangar").string();
  script.exit_help = required(help, "exit").string();
  const auto &buy = required(help, "buy").array();
  if (buy.size() != 2) {
    throw std::runtime_error("help_text.buy: needs YES's and NO's");
  }
  script.buy_help = {buy[0].string(), buy[1].string()};
}

} // namespace

HangarMenuScript parse_hangar_menu(const json::JsonValue &document) {
  try {
    HangarMenuScript script;
    parse_town_basics(document, script);
    const auto &buttons = required(document, "buttons");
    script.left = buttons_from_json(required(buttons, "left"));
    script.right = buttons_from_json(required(buttons, "right"));
    script.up = buttons_from_json(required(buttons, "up"));
    script.down = buttons_from_json(required(buttons, "down"));
    script.riding = buttons_from_json(required(buttons, "riding"));
    parse_sounds(required(document, "sounds"), script);
    script.bands = member(document, "bands", parse_sprites);
    script.cards = member(document, "cards", parse_cards);
    script.plates = member(document, "plates", parse_plates);
    script.robots = member(document, "robots", parse_robots);
    script.menu = member(document, "menu", parse_menu);
    for (const auto &[name, screen] : required(document, "routes").object()) {
      script.routes[action_named(name)] = screen.string();
    }
    script.hangar_price = static_cast<std::int64_t>(required(document, "hangar_price").number());
    script.overweight_margin = number_of(document, "overweight_margin");
    parse_texts(document, script);
    if (const json::JsonValue *origins = document.find("origins")) {
      for (const auto &[name, origin] : origins->object()) {
        script.origins[name] = parse_origin(origin, script.menu);
      }
    }
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("hangar.json: ") + error.what());
  }
}

HangarMenuScript hangar_from(const HangarMenuScript &script, const std::string &origin) {
  HangarMenuScript out = script;
  const auto found = script.origins.find(origin);
  if (found == script.origins.end()) {
    return out;
  }
  if (found->second.exit) {
    out.cards.exit = *found->second.exit;
  }
  if (found->second.exit_help) {
    out.exit_help = *found->second.exit_help;
  }
  if (found->second.menu) {
    out.menu = *found->second.menu;
  }
  out.refuse = found->second.refuse;
  return out;
}

} // namespace phantomcrash
