#include "phantomcrash/screen_adjust.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
namespace {

using json::numbers;
using json::required;

} // namespace

ScreenAdjustScript parse_screen_adjust(const json::JsonValue &document) {
  try {
    ScreenAdjustScript script;
    parse_menu_basics(document, script);
    script.reset_buttons = buttons_from_json(required(required(document, "reset"), "buttons"));
    script.move_x_sound = required(document, "sounds").string_or("move_x", script.move_sound);
    for (std::size_t axis = 0; axis < 2; ++axis) {
      const char *key = axis == 0 ? "range_x" : "range_y";
      const auto range = numbers<2>(required(document, key), key);
      script.minimum[axis] = static_cast<int>(std::lround(range[0]));
      script.maximum[axis] = static_cast<int>(std::lround(range[1]));
      if (script.minimum[axis] > 0 || script.maximum[axis] < 0) {
        throw std::runtime_error(std::string(key) + " must include 0");
      }
    }
    if (const auto *fixed = document.find("fixed")) {
      script.fixed = parse_sprites(*fixed);
    }
    const auto &question = required(document, "question");
    script.question = parse_lines(required(question, "lines"));
    script.default_choice = static_cast<int>(question.number_or("default_choice", 1));
    if (script.default_choice != 0 && script.default_choice != 1) {
      throw std::runtime_error("default_choice must be 0 (YES) or 1 (NO)");
    }
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("screen_adjust.json: ") + error.what());
  }
}

ScreenAdjustMenu::ScreenAdjustMenu(ScreenAdjustScript script, MessageBoxStyle box,
                                   const GameOptions options)
    : script_(std::move(script)), box_(std::move(box)), options_(options),
      fade_(script_.fade_in, script_.fade_out),
      repeat_x_(script_.repeat_delay, script_.repeat_interval),
      repeat_y_(script_.repeat_delay, script_.repeat_interval),
      repeat_choice_(script_.repeat_delay, script_.repeat_interval) {
  // An offset from the settings file outside this screen's range is kept
  // until the player moves it.
  original_ = options_.screen_offset;
}

std::vector<std::string> ScreenAdjustMenu::take_sounds() {
  return std::exchange(sounds_, {});
}

void ScreenAdjustMenu::sound(const std::string &name) {
  if (!name.empty()) {
    sounds_.push_back(name);
  }
}

void ScreenAdjustMenu::update(const double tick_seconds, const InputFrame &input) {
  if (done()) {
    return;
  }
  seconds_ += tick_seconds;
  box_.update(tick_seconds);
  if (!fade_.update(tick_seconds)) {
    return;
  }
  if (box_.is_open()) {
    answer(tick_seconds, input);
  } else {
    adjust(tick_seconds, input);
  }
}

void ScreenAdjustMenu::adjust(const double tick_seconds, const InputFrame &input) {
  // A held direction steps on; the ends do not wrap (the helper's flags 0x13
  // and 0x12 have no wrap bit).
  const auto step_axis = [&](CursorRepeat &repeat, const std::size_t axis, const Buttons back,
                             const Buttons forward, const std::string &move) {
    bool fresh = false;
    const int step = repeat.step(tick_seconds, input, back, forward, fresh);
    int &value = options_.screen_offset[axis];
    const int moved = std::clamp(value + step, std::min(script_.minimum[axis], value),
                                 std::max(script_.maximum[axis], value));
    if (moved != value) {
      value = moved;
      sound(move);
    }
  };
  step_axis(repeat_x_, 0, button::left, button::right, script_.move_x_sound);
  step_axis(repeat_y_, 1, button::up, button::down, script_.move_sound);
  if (input.any_pressed(script_.reset_buttons)) {
    options_.screen_offset = {};
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    sound(script_.select_sound);
    box_.open(MessageBox::Kind::question, script_.question, script_.default_choice);
    sound(box_.style().open_sound);
    repeat_choice_.reset();
  }
}

void ScreenAdjustMenu::answer(const double tick_seconds, const InputFrame &input) {
  bool fresh = false;
  const int step = repeat_choice_.step(tick_seconds, input, button::left, button::right, fresh);
  if (const int moved = move_cursor(box_.choice(), step, fresh, 2); moved != box_.choice()) {
    box_.set_choice(moved);
    sound(script_.move_sound);
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    sound(script_.select_sound);
    if (box_.choice() != 0) {
      options_.screen_offset = original_;
    }
    // The box stays up through the fade out (0x130b4 still draws it).
    fade_.leave(script_.exit_to);
    return;
  }
  if (input.any_pressed(script_.cancel_buttons) && box_.choice() != 1) {
    box_.set_choice(1);
    sound(script_.move_sound);
  }
}

} // namespace phantomcrash
