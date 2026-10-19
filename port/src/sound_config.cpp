#include "phantomcrash/sound_config.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
namespace {

using json::numbers;
using json::required;

constexpr int volume_levels = 256;

Sprite item_sprite(const json::JsonValue &item, const char *key) {
  Sprite sprite = parse_sprite(required(item, key));
  if (const auto *position = item.find("position")) {
    sprite.position = numbers<2>(*position, "position");
  }
  return sprite;
}

DigitStyle parse_digits(const json::JsonValue &value) {
  DigitStyle style;
  style.image = required(value, "image").string();
  const auto &rects = required(value, "rects").array();
  if (rects.size() != style.rects.size()) {
    throw std::runtime_error("digits need 10 rects");
  }
  for (std::size_t digit = 0; digit < rects.size(); ++digit) {
    style.rects[digit] = numbers<4>(rects[digit], "rect");
  }
  style.advance = static_cast<float>(value.number_or("advance", style.advance));
  style.digits = static_cast<int>(value.number_or("digits", style.digits));
  style.layer = static_cast<int>(value.number_or("layer", style.layer));
  style.colour = parse_colour(value.string_or("colour", "#ffffff"));
  return style;
}

SoundItem parse_item(const json::JsonValue &value) {
  SoundItem item;
  item.id = value.string_or("id", "");
  try {
    item.normal = item_sprite(value, "normal");
    item.selected = item_sprite(value, "selected");
    item.help = parse_help(value);
    int actions = 0;
    const auto number = [&](const json::JsonValue &action) {
      item.number = numbers<2>(required(action, "number"), "number");
      item.wrap = action.boolean_or("wrap", true);
    };
    if (const auto *volume = value.find("volume")) {
      item.action = SoundAction::volume;
      item.option = required(*volume, "option").string();
      GameOptions probe;
      if (game_value(probe, item.option) == nullptr) {
        throw std::runtime_error("no setting \"" + item.option + "\"");
      }
      item.count = volume_levels;
      number(*volume);
      if (const auto *bar = volume->find("bar")) {
        item.bar = ValueBar{parse_sprite(*bar), static_cast<float>(bar->number_or("per_step", 1))};
      }
      ++actions;
    }
    if (const auto *test = value.find("test")) {
      item.action = SoundAction::test;
      number(*test);
      ++actions;
    }
    if (value.boolean_or("exit", false)) {
      item.action = SoundAction::exit;
      ++actions;
    }
    if (actions != 1) {
      throw std::runtime_error("needs one of \"volume\", \"test\" or \"exit\"");
    }
  } catch (const std::exception &error) {
    throw std::runtime_error("item \"" + item.id + "\": " + error.what());
  }
  return item;
}

int item_index(const std::vector<SoundItem> &items, const std::string &id, const char *what) {
  const auto found = std::ranges::find(items, id, &SoundItem::id);
  if (found == items.end()) {
    throw std::runtime_error(std::string(what) + " names no item \"" + id + "\"");
  }
  return static_cast<int>(found - items.begin());
}

} // namespace

float volume_gain(const int level, const std::vector<int> &millibels) {
  const int clamped = std::clamp(level, 0, volume_levels - 1);
  double decibels_x100 = 0.0;
  if (millibels.size() == static_cast<std::size_t>(volume_levels)) {
    decibels_x100 = millibels[static_cast<std::size_t>(clamped)];
    if (decibels_x100 <= -9600.0) {
      return 0.0F;
    }
  } else {
    if (clamped == 0) {
      return 0.0F;
    }
    decibels_x100 = 2285.0 * std::log10(clamped / 255.0);
  }
  return static_cast<float>(std::pow(10.0, decibels_x100 / 2000.0));
}

std::string test_sound_file(const std::vector<TestGroup> &groups, int number) {
  for (const auto &group : groups) {
    if (number < group.count) {
      char name[16];
      std::snprintf(name, sizeof(name), "/%02d.wav", number);
      return group.folder + name;
    }
    number -= group.count;
  }
  return {};
}

SoundConfigScript parse_sound_config(const json::JsonValue &document) {
  try {
    SoundConfigScript script;
    parse_menu_basics(document, script);
    static_cast<MenuTickers &>(script) = parse_menu_tickers(document);
    const auto &value_repeat = required(document, "value_repeat");
    script.value_delay = required(value_repeat, "delay").number();
    script.value_interval = required(value_repeat, "interval").number();
    script.digits = parse_digits(required(document, "digits"));
    int tests = 0;
    for (const auto &group : required(document, "test_sounds").array()) {
      script.test_groups.push_back({required(group, "folder").string(),
                                    static_cast<int>(required(group, "count").number())});
      tests += script.test_groups.back().count;
    }
    for (const auto &item : required(document, "items").array()) {
      script.items.push_back(parse_item(item));
      if (script.items.back().action == SoundAction::test) {
        script.items.back().count = std::max(tests, 1);
      }
    }
    if (script.items.empty()) {
      throw std::runtime_error("needs at least one item");
    }
    script.default_cursor =
        item_index(script.items, required(document, "default_cursor").string(), "default_cursor");
    script.cancel_cursor =
        item_index(script.items, required(document, "cancel_cursor").string(), "cancel_cursor");
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("sound_config.json: ") + error.what());
  }
}

SoundConfigMenu::SoundConfigMenu(SoundConfigScript script, const GameOptions options)
    : script_(std::move(script)), options_(options), fade_(script_.fade_in, script_.fade_out),
      cursor_(script_.default_cursor), help_(script_.items.size()),
      repeat_(script_.repeat_delay, script_.repeat_interval),
      value_repeat_(script_.value_delay, script_.value_interval) {}

std::vector<std::string> SoundConfigMenu::take_sounds() {
  return std::exchange(sounds_, {});
}

void SoundConfigMenu::sound(const std::string &name) {
  if (!name.empty()) {
    sounds_.push_back(name);
  }
}

int *SoundConfigMenu::value_of(const SoundItem &item) {
  switch (item.action) {
  case SoundAction::volume:
    return game_value(options_, item.option);
  case SoundAction::test:
    return &test_;
  case SoundAction::exit:
    break;
  }
  return nullptr;
}

int SoundConfigMenu::value(const int item) const {
  const auto &entry = script_.items.at(static_cast<std::size_t>(item));
  switch (entry.action) {
  case SoundAction::volume:
    return *game_value(options_, entry.option);
  case SoundAction::test:
    return test_;
  case SoundAction::exit:
    break;
  }
  return 0;
}

void SoundConfigMenu::update(const double tick_seconds, const InputFrame &input) {
  if (done()) {
    return;
  }
  seconds_ += tick_seconds;
  help_.advance(cursor_, tick_seconds);
  if (!fade_.update(tick_seconds)) {
    return;
  }
  const int count = static_cast<int>(script_.items.size());
  bool fresh = false;
  const int step = repeat_.step(tick_seconds, input, button::up, button::down, fresh);
  if (const int moved = move_cursor(cursor_, step, fresh, count); moved != cursor_) {
    cursor_ = moved;
    help_.restart(cursor_);
    value_repeat_.reset();
    sound(script_.move_sound);
  }

  const SoundItem &item = script_.items[static_cast<std::size_t>(cursor_)];
  if (int *value = value_of(item)) {
    const int horizontal =
        value_repeat_.step(tick_seconds, input, button::left, button::right, fresh);
    const int moved = item.wrap ? move_cursor(*value, horizontal, fresh, item.count)
                                : std::clamp(*value + horizontal, 0, item.count - 1);
    if (moved != *value) {
      *value = moved;
      sound(script_.move_sound);
    }
  }

  // Confirm makes no sound of its own here (0xdf310): the test plays its
  // effect, EXIT the select sound.
  if (input.any_pressed(script_.confirm_buttons)) {
    if (item.action == SoundAction::test) {
      sound(test_sound_file(script_.test_groups, test_));
    } else if (item.action == SoundAction::exit) {
      sound(script_.select_sound);
      fade_.leave(script_.exit_to);
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons) && cursor_ != script_.cancel_cursor) {
    cursor_ = script_.cancel_cursor;
    sound(script_.move_sound);
  }
}

} // namespace phantomcrash
