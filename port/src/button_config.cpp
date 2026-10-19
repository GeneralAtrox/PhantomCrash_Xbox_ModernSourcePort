#include "phantomcrash/button_config.hpp"

#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/shop_pointer.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
namespace {

using json::numbers;
using json::required;

Sprite placed(Sprite sprite, const std::array<float, 2> &position) {
  sprite.position = position;
  return sprite;
}

// One sprite per bindable button, keyed by the button's name.
std::array<Sprite, control_action_count> per_button(const json::JsonValue &value,
                                                    const char *what) {
  std::array<Sprite, control_action_count> sprites;
  for (std::size_t index = 0; index < control_action_count; ++index) {
    const std::string name(bindable_button_names[index]);
    const auto *found = value.find(name.c_str());
    if (found == nullptr) {
      throw std::runtime_error(std::string(what) + " needs \"" + name + "\"");
    }
    sprites[index] = parse_sprite(*found);
  }
  return sprites;
}

ButtonRow parse_row(const json::JsonValue &value) {
  ButtonRow row;
  row.id = value.string_or("id", "");
  try {
    row.bar = numbers<2>(required(value, "bar"), "bar");
    if (const auto *normal = value.find("bar_normal")) {
      row.bar_normal = parse_sprite(*normal);
      row.bar_selected = parse_sprite(required(value, "bar_selected"));
    }
    row.label = parse_sprite(required(value, "label"));
    row.help = parse_help(value);
    int kinds = 0;
    if (const auto *choice = value.find("choice")) {
      row.kind = ButtonRowKind::choice;
      row.option = required(*choice, "option").string();
      GameOptions probe;
      if (game_value(probe, row.option) == nullptr) {
        throw std::runtime_error("no setting \"" + row.option + "\"");
      }
      row.values = parse_sprites(required(*choice, "values"));
      if (row.values.size() != 2) {
        throw std::runtime_error("a choice needs 2 values");
      }
      row.right_arrow = numbers<2>(required(*choice, "right_arrow"), "right_arrow");
      row.left_arrow = numbers<2>(required(*choice, "left_arrow"), "left_arrow");
      ++kinds;
    }
    if (const auto *binding = value.find("binding")) {
      row.kind = ButtonRowKind::binding;
      const auto action = required(*binding, "action").string();
      row.action = control_action_index(action);
      if (row.action < 0) {
        throw std::runtime_error("no action \"" + action + "\"");
      }
      row.icon_position = numbers<2>(required(*binding, "icon"), "icon");
      ++kinds;
    }
    if (value.boolean_or("reset", false)) {
      row.kind = ButtonRowKind::reset;
      ++kinds;
    }
    if (value.boolean_or("exit", false)) {
      row.kind = ButtonRowKind::exit;
      ++kinds;
    }
    if (kinds != 1) {
      throw std::runtime_error("needs one of \"choice\", \"binding\", \"reset\" or \"exit\"");
    }
  } catch (const std::exception &error) {
    throw std::runtime_error("row \"" + row.id + "\": " + error.what());
  }
  return row;
}

} // namespace

ButtonConfigScript parse_button_config(const json::JsonValue &document) {
  try {
    ButtonConfigScript script;
    parse_menu_basics(document, script);
    static_cast<MenuTickers &>(script) = parse_menu_tickers(document);
    const auto &bars = required(document, "bars");
    script.bar_normal = parse_sprite(required(bars, "normal"));
    script.bar_selected = parse_sprite(required(bars, "selected"));
    script.icons = per_button(required(document, "icons"), "icons");
    script.markers = per_button(required(document, "markers"), "markers");
    const auto &arrows = required(document, "arrows");
    script.right_arrow = parse_sprites(required(arrows, "right"));
    script.left_arrow = parse_sprites(required(arrows, "left"));
    for (const auto &row : required(document, "rows").array()) {
      script.rows.push_back(parse_row(row));
    }
    if (script.rows.empty()) {
      throw std::runtime_error("needs at least one row");
    }
    std::array<bool, control_action_count> seen{};
    for (const auto &row : script.rows) {
      if (row.kind == ButtonRowKind::binding) {
        if (seen[static_cast<std::size_t>(row.action)]) {
          throw std::runtime_error("row \"" + row.id + "\" repeats an action");
        }
        seen[static_cast<std::size_t>(row.action)] = true;
      }
    }
    const auto cursor = required(document, "default_cursor").string();
    const auto found = std::ranges::find(script.rows, cursor, &ButtonRow::id);
    if (found == script.rows.end()) {
      throw std::runtime_error("default_cursor names no row \"" + cursor + "\"");
    }
    script.default_cursor = static_cast<int>(found - script.rows.begin());
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("button_config.json: ") + error.what());
  }
}

ButtonConfigMenu::ButtonConfigMenu(ButtonConfigScript script, const GameOptions options)
    : script_(std::move(script)), options_(options), fade_(script_.fade_in, script_.fade_out),
      cursor_(script_.default_cursor), help_(script_.rows.size()),
      repeat_(script_.repeat_delay, script_.repeat_interval),
      choice_repeat_(script_.repeat_delay, script_.repeat_interval) {}

std::vector<std::string> ButtonConfigMenu::take_sounds() {
  return std::exchange(sounds_, {});
}

void ButtonConfigMenu::sound(const std::string &name) {
  if (!name.empty()) {
    sounds_.push_back(name);
  }
}

int ButtonConfigMenu::choice(const int row) const {
  const auto &entry = script_.rows.at(static_cast<std::size_t>(row));
  const int *value = game_value(options_, entry.option);
  return value == nullptr ? 0 : *value;
}

int ButtonConfigMenu::binding(const int row) const {
  const auto &entry = script_.rows.at(static_cast<std::size_t>(row));
  return entry.action < 0 ? -1 : options_.controls.bindings[static_cast<std::size_t>(entry.action)];
}

void ButtonConfigMenu::assign(const int action, const int button_index) {
  auto &bindings = options_.controls.bindings;
  const auto own = static_cast<std::size_t>(action);
  for (std::size_t other = 0; other < bindings.size(); ++other) {
    if (other != own && bindings[other] == button_index) {
      bindings[other] = bindings[own];
      bindings[own] = button_index;
      return;
    }
  }
}

std::optional<int> ButtonConfigMenu::pointed_row(const InputFrame &input) const {
  // From the bottom up: each bar's clear lower edge lies under the next row.
  for (int index = static_cast<int>(script_.rows.size()) - 1; input.pointer && index >= 0;
       --index) {
    const ButtonRow &row = script_.rows[static_cast<std::size_t>(index)];
    if (shop_hit(input, placed(row.bar_normal.value_or(script_.bar_normal), row.bar))) {
      return index;
    }
  }
  return std::nullopt;
}

void ButtonConfigMenu::update(const double tick_seconds, const InputFrame &input) {
  if (done()) {
    return;
  }
  seconds_ += tick_seconds;
  help_.advance(cursor_, tick_seconds);
  if (!fade_.update(tick_seconds)) {
    return;
  }
  const int count = static_cast<int>(script_.rows.size());
  // The mouse (a port addition): moving onto a row's bar moves the cursor
  // there, and a click on it confirms it, or on a choice row steps its
  // value (back on the left arrow).
  const std::optional<int> pointed = pointed_row(input);
  bool fresh = false;
  const int step = repeat_.step(tick_seconds, input, button::up, button::down, fresh);
  int target = move_cursor(cursor_, step, fresh, count);
  if (pointed && (input.pointer_moved || input.clicked)) {
    target = *pointed;
  }
  if (const int moved = target; moved != cursor_) {
    cursor_ = moved;
    help_.restart(cursor_);
    choice_repeat_.reset();
    sound(script_.move_sound);
  }

  const ButtonRow &row = script_.rows[static_cast<std::size_t>(cursor_)];
  if (row.kind == ButtonRowKind::choice) {
    int &value = *game_value(options_, row.option);
    int horizontal = choice_repeat_.step(tick_seconds, input, button::left, button::right, fresh);
    if (input.clicked && pointed == cursor_) {
      horizontal = shop_hit(input, placed(script_.left_arrow.front(), row.left_arrow)) ? -1 : 1;
      fresh = true;
    }
    if (const int moved = move_cursor(value, horizontal, fresh, 2); moved != value) {
      value = moved;
      sound(script_.move_sound);
    }
  } else if (row.kind == ButtonRowKind::binding) {
    // The first assignable button pressed, in the original's order; silent.
    for (std::size_t index = 0; index < control_action_count; ++index) {
      if (input.any_pressed(bindable_buttons[index])) {
        assign(row.action, static_cast<int>(index));
        break;
      }
    }
  }

  // Confirm makes no sound of its own here (0xdf310); DEFAULT and EXIT play
  // the select sound.
  if (input.any_pressed(script_.confirm_buttons) || (input.clicked && pointed == cursor_)) {
    if (row.kind == ButtonRowKind::reset) {
      sound(script_.select_sound);
      options_.controls.bindings = ControlOptions{}.bindings;
    } else if (row.kind == ButtonRowKind::exit) {
      sound(script_.select_sound);
      fade_.leave(script_.exit_to);
    }
  }
}

} // namespace phantomcrash
