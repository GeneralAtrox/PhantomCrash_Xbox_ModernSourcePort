// Parsing of data/ui/vs_mode.json.

#include "phantomcrash/vs_mode_script.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

namespace phantomcrash {
namespace {

using json::member;
using json::numbers;
using json::required;

double number_of(const json::JsonValue &value, const char *key) {
  return required(value, key).number();
}

float float_of(const json::JsonValue &value, const char *key) {
  return static_cast<float>(number_of(value, key));
}

std::vector<Sprite> sprites_of(const json::JsonValue &value) {
  std::vector<Sprite> sprites;
  for (const json::JsonValue &sprite : value.array()) {
    sprites.push_back(parse_sprite(sprite));
  }
  return sprites;
}

std::vector<float> floats_of(const json::JsonValue &value) {
  std::vector<float> out;
  for (const json::JsonValue &number : value.array()) {
    out.push_back(static_cast<float>(number.number()));
  }
  return out;
}

ScaleAnchor anchor_named(const std::string &name) {
  static const std::map<std::string, ScaleAnchor> anchors{
      {"centre", ScaleAnchor::centre},           {"bottom_right", ScaleAnchor::bottom_right},
      {"bottom_left", ScaleAnchor::bottom_left}, {"top_right", ScaleAnchor::top_right},
      {"top_left", ScaleAnchor::top_left},
  };
  const auto found = anchors.find(name);
  if (found == anchors.end()) {
    throw std::runtime_error("no anchor " + name);
  }
  return found->second;
}

VsPlayerLayout parse_player(const json::JsonValue &value) {
  VsPlayerLayout player;
  const std::pair<const char *, Sprite VsPlayerLayout::*> sprites[] = {
      {"panel", &VsPlayerLayout::panel},
      {"label", &VsPlayerLayout::label},
      {"enter_now", &VsPlayerLayout::enter_now},
      {"entered", &VsPlayerLayout::entered},
      {"entered_box", &VsPlayerLayout::entered_box},
      {"box", &VsPlayerLayout::box},
      {"hint", &VsPlayerLayout::hint},
      {"sv_name", &VsPlayerLayout::sv_name},
      {"win_lose", &VsPlayerLayout::win_lose},
      {"strip", &VsPlayerLayout::strip},
      {"arrow_left", &VsPlayerLayout::arrow_left},
      {"arrow_right", &VsPlayerLayout::arrow_right},
      {"your_sv", &VsPlayerLayout::your_sv},
      {"no_controller", &VsPlayerLayout::no_controller},
      {"no_entry", &VsPlayerLayout::no_entry},
  };
  for (const auto &[key, field] : sprites) {
    player.*field = member(value, key, parse_sprite);
  }
  player.wins = member(value, "wins", parse_number_style);
  player.losses = member(value, "losses", parse_number_style);
  player.cards_at = numbers<2>(required(value, "cards_at"), "cards_at");
  player.clip = numbers<2>(required(value, "clip"), "clip");
  player.name_at = numbers<2>(required(value, "name_at"), "name_at");
  player.icon_at = numbers<2>(required(value, "icon_at"), "icon_at");
  player.chosen_name_at = numbers<2>(required(value, "chosen_name_at"), "chosen_name_at");
  player.grow_from = anchor_named(required(value, "grow_from").string());
  player.hint_from_bottom = required(value, "hint_from").string() == "bottom";
  return player;
}

VsCards parse_cards(const json::JsonValue &value) {
  VsCards cards;
  cards.visible = static_cast<int>(number_of(value, "visible"));
  cards.step = float_of(value, "step");
  cards.scroll_seconds = number_of(value, "scroll_seconds");
  cards.push = float_of(value, "push");
  for (const json::JsonValue &card : required(value, "makers").array()) {
    cards.makers.push_back(parse_choice(card));
  }
  cards.empty = member(value, "empty", parse_choice);
  cards.icons = sprites_of(required(value, "icons"));
  for (const json::JsonValue &preset : required(value, "presets").array()) {
    cards.presets.push_back({static_cast<int>(number_of(preset, "robot")),
                             member(preset, "card", parse_choice),
                             member(preset, "icon", parse_sprite)});
  }
  if (cards.visible < 1 || cards.makers.size() != cards.icons.size()) {
    throw std::runtime_error("cards: needs a visible card and an icon for each maker's card");
  }
  return cards;
}

VsPopup parse_popup(const json::JsonValue &value) {
  return {member(value, "window", parse_sprite),   member(value, "entry", parse_sprite),
          member(value, "question", parse_sprite), member(value, "select", parse_sprite),
          member(value, "yes", parse_choice),      member(value, "no", parse_choice)};
}

VsIntro parse_intro(const json::JsonValue &value) {
  return {number_of(value, "grow_seconds"), number_of(value, "shrink_from"),
          number_of(value, "end"), number_of(value, "stagger"), number_of(value, "settle_seconds")};
}

VsTimings parse_timings(const json::JsonValue &question, const json::JsonValue &panel) {
  VsTimings timings;
  timings.question_grow = number_of(question, "grow_seconds");
  timings.leave_seconds = number_of(question, "leave_seconds");
  timings.fade_seconds = number_of(question, "fade_seconds");
  timings.exit_seconds = number_of(question, "area_seconds");
  timings.panel_grow = number_of(panel, "grow_seconds");
  timings.label_seconds = number_of(panel, "label_seconds");
  timings.open_seconds = number_of(panel, "open_seconds");
  timings.joined_colour = parse_colour(required(panel, "joined_colour").string());
  return timings;
}

VsAreaKind kind_named(const std::string &name) {
  if (name == "area") {
    return VsAreaKind::area;
  }
  if (name == "random") {
    return VsAreaKind::random;
  }
  if (name == "exit") {
    return VsAreaKind::exit;
  }
  throw std::runtime_error("no area kind " + name);
}

VsAreaItem parse_item(const json::JsonValue &value) {
  VsAreaItem item;
  item.id = required(value, "id").string();
  item.text = value.string_or("text", "");
  item.kind = kind_named(required(value, "kind").string());
  item.picture = member(value, "picture", parse_sprite);
  item.night_picture = member(value, "night_picture", parse_sprite);
  item.label = member(value, "label", parse_sprite);
  item.title = member(value, "title", parse_sprite);
  item.item = member(value, "item", parse_choice);
  item.from_x = float_of(value, "from_x");
  if (item.kind == VsAreaKind::area) {
    const auto stages = numbers<2>(required(value, "stages"), "stages");
    item.stages = {static_cast<int>(stages[0]), static_cast<int>(stages[1])};
  }
  return item;
}

VsCrowd parse_crowd(const json::JsonValue &value, const std::size_t items) {
  VsCrowd crowd;
  crowd.players = static_cast<int>(number_of(value, "players"));
  crowd.first = static_cast<int>(number_of(value, "first"));
  crowd.xs = floats_of(required(value, "xs"));
  crowd.from_xs = floats_of(required(value, "from_xs"));
  for (const float stage : floats_of(required(value, "stages"))) {
    crowd.stages.push_back(static_cast<int>(stage));
  }
  const std::size_t shown = items - static_cast<std::size_t>(std::max(crowd.first, 0));
  if (crowd.first < 0 || crowd.xs.size() != shown || crowd.from_xs.size() != shown ||
      crowd.stages.empty()) {
    throw std::runtime_error("crowd: needs a place for each item from first, and stages");
  }
  return crowd;
}

VsTimeZone parse_time_zone(const json::JsonValue &value) {
  VsTimeZone zone;
  zone.window = member(value, "window", parse_sprite);
  zone.day = member(value, "day", parse_choice);
  zone.night = member(value, "night", parse_choice);
  zone.dim_colour = parse_colour(required(value, "dim_colour").string());
  zone.grow_seconds = number_of(value, "grow_seconds");
  return zone;
}

VsAreaSelect parse_area(const json::JsonValue &value) {
  VsAreaSelect area;
  area.layers = sprites_of(required(value, "layers"));
  area.picture_frame = member(value, "picture_frame", parse_sprite);
  area.bar = member(value, "bar", parse_sprite);
  area.bar_offset = float_of(value, "bar_offset");
  area.slide_seconds = number_of(value, "slide_seconds");
  area.fade_seconds = number_of(value, "fade_seconds");
  for (const json::JsonValue &item : required(value, "items").array()) {
    area.items.push_back(parse_item(item));
  }
  if (area.items.empty() || area.items.back().kind != VsAreaKind::exit) {
    throw std::runtime_error("items: needs EXIT last");
  }
  area.crowd = parse_crowd(required(value, "crowd"), area.items.size());
  const auto &random = required(value, "random");
  area.random_every = number_of(random, "every_seconds");
  area.random_pictures = sprites_of(required(random, "pictures"));
  area.time_zone = member(value, "time_zone", parse_time_zone);
  return area;
}

void parse_sounds(const json::JsonValue &sounds, VsModeScript &script) {
  script.no_sound = sounds.string_or("no", "");
  script.unusable_sound = sounds.string_or("unusable", script.no_sound);
  script.choose_sound = sounds.string_or("choose", "");
  script.to_no_sound = sounds.string_or("to_no", "");
  script.slide_sound = sounds.string_or("slide", "");
}

void parse_players(const json::JsonValue &value, VsModeScript &script) {
  const auto &players = value.array();
  if (players.size() != script.players.size()) {
    throw std::runtime_error("players: needs one for each controller port");
  }
  for (std::size_t index = 0; index < players.size(); ++index) {
    script.players[index] = parse_player(players[index]);
  }
}

} // namespace

VsModeScript parse_vs_mode(const json::JsonValue &document) {
  try {
    VsModeScript script;
    parse_town_basics(document, script);
    script.battle_route = required(required(document, "routes"), "battle").string();
    const auto &buttons = required(document, "buttons");
    script.left = buttons_from_json(required(buttons, "left"));
    script.right = buttons_from_json(required(buttons, "right"));
    parse_sounds(required(document, "sounds"), script);
    script.backdrop = sprites_of(required(document, "backdrop"));
    const auto &hint = required(document, "phase_hint");
    script.entry_hint = member(hint, "entry", parse_sprite);
    script.select_hint = member(hint, "select", parse_sprite);
    script.intro = member(document, "intro", parse_intro);
    script.popup = member(document, "popup", parse_popup);
    script.timings = parse_timings(required(document, "question"), required(document, "panel"));
    parse_players(required(document, "players"), script);
    script.cards = member(document, "cards", parse_cards);
    const auto &text = required(document, "text");
    script.text_colour = parse_colour(required(text, "colour").string());
    script.name_layer = static_cast<int>(number_of(text, "name_layer"));
    script.chosen_layer = static_cast<int>(number_of(text, "chosen_layer"));
    script.record_font = member(document, "record_font", parse_number_font);
    script.record_layer = static_cast<int>(number_of(document, "record_layer"));
    script.area = member(document, "area_select", parse_area);
    script.message_text_at = numbers<2>(required(document, "message_text_at"), "message_text_at");
    for (const auto &[id, lines] : required(document, "messages").object()) {
      script.messages[id] = parse_lines(lines);
    }
    script.help_style = member(document, "help", parse_ticker);
    for (const char *phase : {"entry", "select", "area"}) {
      script.helps[phase] = required(required(document, "helps"), phase).string();
    }
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("vs_mode.json: ") + error.what());
  }
}

} // namespace phantomcrash
