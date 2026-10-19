// Parsing of data/ui/chip_shop.json.

#include "phantomcrash/chip_shop_script.hpp"

#include "phantomcrash/json_fields.hpp"

#include <cctype>
#include <stdexcept>
#include <string_view>

namespace phantomcrash {
namespace {

// The game's CHIP descriptions (chip_help, read from its executable) space
// their sight brackets unevenly: "sight 》and", "and《aiming", doubled
// spaces. The port shows them evenly: no space inside a closing bracket, one
// before an opening bracket that follows a word, single spaces.
std::string tidy_brackets(std::string_view text) {
  constexpr std::string_view open = "《";
  constexpr std::string_view close = "》";
  std::string out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const std::string_view rest = text.substr(i);
    if (text[i] == ' ' && !out.empty() && out.back() == ' ') {
      continue;
    }
    if (rest.starts_with(close)) {
      while (!out.empty() && out.back() == ' ') {
        out.pop_back();
      }
    }
    if (rest.starts_with(open) && !out.empty() &&
        (std::isalnum(static_cast<unsigned char>(out.back())) != 0 || out.back() == '_')) {
      out += ' ';
    }
    out += text[i];
  }
  return out;
}

using json::member;
using json::numbers;
using json::required;

float number_of(const json::JsonValue &value, const char *key) {
  return static_cast<float>(required(value, key).number());
}

int integer_of(const json::JsonValue &value, const char *key) {
  return static_cast<int>(required(value, key).number());
}

double seconds_of(const json::JsonValue &value, const char *key) {
  const double seconds = required(value, key).number();
  if (seconds <= 0.0) {
    throw std::runtime_error(std::string(key) + " must be above 0");
  }
  return seconds;
}

template <std::size_t N> std::array<Sprite, N> sprite_array(const json::JsonValue &value) {
  const auto sprites = parse_sprites(value);
  if (sprites.size() != N) {
    throw std::runtime_error("needs " + std::to_string(N) + " sprites");
  }
  std::array<Sprite, N> out;
  for (std::size_t index = 0; index < N; ++index) {
    out[index] = sprites[index];
  }
  return out;
}

template <std::size_t N> std::array<NumberStyle, N> number_array(const json::JsonValue &value) {
  const auto &items = value.array();
  if (items.size() != N) {
    throw std::runtime_error("needs " + std::to_string(N) + " numbers");
  }
  std::array<NumberStyle, N> out;
  for (std::size_t index = 0; index < N; ++index) {
    out[index] = parse_number_style(items[index]);
  }
  return out;
}

ChipCards parse_cards(const json::JsonValue &value) {
  ChipCards cards;
  cards.card = member(value, "card", parse_sprite);
  cards.centre = numbers<2>(required(value, "centre"), "centre");
  cards.step = number_of(value, "step");
  cards.side = integer_of(value, "side");
  cards.side_scale = number_of(value, "side_scale");
  cards.side_drop = number_of(value, "side_drop");
  cards.shade = integer_of(value, "shade");
  cards.clip = numbers<4>(required(value, "clip"), "clip");
  cards.face_drop = number_of(value, "face_drop");
  cards.face_layer = integer_of(value, "face_layer");
  cards.slide_seconds = seconds_of(value, "slide_seconds");
  cards.fade_seconds = seconds_of(value, "fade_seconds");
  if (cards.side < 0) {
    throw std::runtime_error("cards: side must be 0 or more");
  }
  return cards;
}

ChipDetails parse_details(const json::JsonValue &value) {
  ChipDetails details;
  const auto &name = required(value, "name");
  details.name_at = numbers<2>(required(name, "position"), "position");
  details.name_colour = parse_colour(required(name, "colour").string());
  details.name_layer = integer_of(name, "layer");
  details.price = member(value, "price", parse_number_style);
  details.level_label = member(value, "level_label", parse_sprite);
  details.level = member(value, "level", parse_number_style);
  details.weight_label = member(value, "weight_label", parse_sprite);
  details.weight = member(value, "weight", parse_number_style);
  details.weight_unit = member(value, "weight_unit", parse_sprite);
  return details;
}

ChipCounter parse_counter(const json::JsonValue &value) {
  ChipCounter counter;
  counter.index = member(value, "index", parse_number_style);
  counter.total = member(value, "total", parse_number_style);
  counter.dashes = member(value, "dashes", parse_sprites);
  return counter;
}

ChipSpecPanel parse_spec(const json::JsonValue &value) {
  ChipSpecPanel spec;
  spec.labels = member(value, "labels", parse_sprite);
  spec.inner_shapes = member(value, "inner_shapes", sprite_array<3>);
  spec.outer_shapes = member(value, "outer_shapes", sprite_array<3>);
  spec.inner_size = member(value, "inner_size", number_array<2>);
  spec.outer_size = member(value, "outer_size", number_array<2>);
  spec.centre = numbers<2>(required(value, "centre"), "centre");
  spec.preview_scale = number_of(value, "preview_scale");
  spec.inner_corners = member(value, "inner_corners", sprite_array<4>);
  spec.outer_corners = member(value, "outer_corners", sprite_array<4>);
  spec.inner_fill = member(value, "inner_fill", parse_sprite);
  spec.outer_fill = member(value, "outer_fill", parse_sprite);
  spec.mortal_shot = member(value, "mortal_shot", parse_number_style);
  spec.repulsion_shield = member(value, "repulsion_shield", parse_number_style);
  return spec;
}

ChipDialog parse_dialog(const json::JsonValue &value) {
  ChipDialog dialog;
  const auto &stages = required(value, "stage_seconds").array();
  if (stages.size() != dialog.stage_seconds.size()) {
    throw std::runtime_error("stage_seconds: needs three stages");
  }
  for (std::size_t stage = 0; stage < stages.size(); ++stage) {
    dialog.stage_seconds[stage] = stages[stage].number();
    if (dialog.stage_seconds[stage] <= 0.0) {
      throw std::runtime_error("stage_seconds must be above 0");
    }
  }
  dialog.card_shift = number_of(value, "card_shift");
  dialog.panel = member(value, "panel", parse_sprite);
  dialog.panel_open_layer = integer_of(value, "panel_open_layer");
  dialog.panel_to = numbers<2>(required(value, "panel_to"), "panel_to");
  dialog.pops = member(value, "pops", parse_sprites);
  dialog.pop_thin = number_of(value, "pop_thin");
  dialog.spec = member(value, "spec", parse_spec);
  if (const auto *rows = value.find("rows")) {
    dialog.rows = parse_chip_spec_rows(*rows);
  }
  if (const auto *browse = value.find("browse")) {
    const auto arrows = member(*browse, "arrows", parse_sprites);
    if (arrows.size() != 2) {
      throw std::runtime_error("browse.arrows: needs left and right");
    }
    dialog.browse = ChipDialogBrowse{{arrows[0], arrows[1]}, number_of(*browse, "slide")};
  }
  return dialog;
}

void parse_window(const json::JsonValue &document, ChipShopScript &script) {
  const auto &window = required(document, "window");
  script.window = member(window, "sprite", parse_sprite);
  script.window_seconds = seconds_of(window, "grow_seconds");
  script.window_widest = number_of(window, "widest");
  script.window_thin = number_of(window, "thin");
  script.window_dim = parse_colour(required(window, "dim").string());
  script.frame = member(window, "frame", parse_sprites);
  const auto &scan = required(document, "scanlines");
  script.scanlines = member(scan, "sprite", parse_sprite);
  script.scan_from = number_of(scan, "from");
  script.scan_period = number_of(scan, "period");
  script.scan_speed = number_of(scan, "speed");
  script.scan_clip = numbers<4>(required(scan, "clip"), "clip");
  if (script.scan_period <= 0.0F) {
    throw std::runtime_error("scanlines: period must be above 0");
  }
  script.highlight = member(document, "highlight", parse_sprite);
  script.row_arrows = member(document, "row_arrows", parse_sprites);
}

ChipLevelWindow parse_level_window(const json::JsonValue &value) {
  ChipLevelWindow level;
  level.window = member(value, "window", parse_sprite);
  level.bar = member(value, "bar", parse_sprite);
  level.bar_at = numbers<2>(required(value, "bar_at"), "bar_at");
  level.bar_step = number_of(value, "bar_step");
  level.bar_height = number_of(value, "bar_height");
  level.old_level = member(value, "old_level", parse_number_style);
  level.new_level = member(value, "new_level", parse_number_style);
  level.price = member(value, "price", parse_number_style);
  level.poor_colour = parse_colour(required(value, "poor_colour").string());
  level.cancel_hint = member(value, "cancel_hint", parse_sprites);
  if (const auto *hint = value.find("confirm_hint")) {
    level.confirm_hint = parse_sprites(*hint);
  }
  return level;
}

// The mode's own: its exit, options (or UPLIFT's level window), notices,
// extra labels and slash.
void parse_mode(const json::JsonValue &mode, ChipShopScript &script) {
  script.exit_to = mode.string_or("exit_to", script.exit_to);
  for (Sprite &label : member(mode, "labels", parse_sprites)) {
    script.labels.push_back(std::move(label));
  }
  script.counter.slash = member(mode, "slash", parse_sprite);
  if (script.mode == ChipShopMode::uplift) {
    script.dialog.level = member(mode, "level_window", parse_level_window);
    script.level_max = static_cast<int>(mode.number_or("level_max", script.level_max));
    script.dialog.browse.reset(); // left/right choose the level
  } else {
    script.dialog.options = member(mode, "options", parse_shop_icons);
    if (script.dialog.options.size() != 2) {
      throw std::runtime_error("options: needs the trade and CANCEL");
    }
  }
  for (const auto &[id, lines] : required(mode, "messages").object()) {
    script.messages[id] = parse_lines(lines);
  }
  script.sell_rate = mode.number_or("sell_rate", script.sell_rate);
}

} // namespace

ChipShopScript parse_chip_shop(const json::JsonValue &document, const ChipShopMode mode) {
  const char *mode_names[] = {"buy", "sell", "uplift"};
  const char *mode_name = mode_names[static_cast<int>(mode)];
  const char *file = mode == ChipShopMode::uplift ? "uplift.json: " : "chip_shop.json: ";
  try {
    ChipShopScript script;
    script.mode = mode;
    parse_town_basics(document, script);
    const auto &buttons = required(document, "buttons");
    script.left = buttons_from_json(required(buttons, "left"));
    script.right = buttons_from_json(required(buttons, "right"));
    script.up = buttons_from_json(required(buttons, "up"));
    script.down = buttons_from_json(required(buttons, "down"));
    const auto &sounds = required(document, "sounds");
    script.row_sound = sounds.string_or("row", "");
    script.choice_sound = sounds.string_or("choice", "");
    script.jump_sound = sounds.string_or("jump", "");
    script.trade_sound = sounds.string_or("trade", "");
    script.back_sound = sounds.string_or("back", "");
    parse_window(document, script);
    script.cards = member(document, "cards", parse_cards);
    script.details = member(document, "details", parse_details);
    script.counter = member(document, "counter", parse_counter);
    script.labels = member(document, "labels", parse_sprites);
    script.exit = member(document, "exit", parse_shop_icon);
    script.dialog = member(document, "dialog", parse_dialog);
    if (const auto *control = document.find("level_control")) {
      script.level_control = parse_level_control(*control);
    }
    script.message_text_at = numbers<2>(required(document, "message_text_at"), "message_text_at");
    script.help_style = member(document, "help", parse_ticker);
    const auto &help_text = required(document, "help_text");
    script.exit_help = help_text.string_or("exit", "");
    script.dialog_help = help_text.string_or("dialog", "");
    for (const auto &line : required(document, "chip_help").array()) {
      script.chip_help.push_back(tidy_brackets(line.string()));
    }
    const auto &modes = required(document, "modes");
    try {
      parse_mode(required(modes, mode_name), script);
    } catch (const std::exception &error) {
      throw std::runtime_error(std::string("modes.") + mode_name + ": " + error.what());
    }
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string(file) + error.what());
  }
}

void use_reticle_sizes(ChipShopScript &script, const BattleHudStyle &hud) {
  ChipSpecPanel &spec = script.dialog.spec;
  spec.inner_sizes = hud.reticle.inner_sizes;
  spec.outer_sizes = hud.reticle.outer_sizes;
  spec.value_max = hud.control.value_max;
}

} // namespace phantomcrash
