#include "phantomcrash/level_control.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace phantomcrash {

LevelControlStyle parse_level_control(const json::JsonValue &value) {
  LevelControlStyle style;
  style.delay = value.number_or("delay", style.delay);
  style.interval = value.number_or("interval", style.interval);
  style.accelerate_after = value.number_or("accelerate_after", style.accelerate_after);
  style.fast_interval = value.number_or("fast_interval", style.fast_interval);
  style.sound_interval = value.number_or("sound_interval", style.sound_interval);
  style.coarse_step = static_cast<int>(value.number_or("coarse_step", style.coarse_step));
  for (auto [name, target] : {std::pair{"coarse_back", &style.coarse_back},
                              {"coarse_forward", &style.coarse_forward},
                              {"minimum", &style.minimum},
                              {"maximum", &style.maximum}}) {
    if (const auto *buttons = value.find(name)) {
      *target = buttons_from_json(*buttons);
    }
  }
  if (!(style.delay > 0 && style.interval > 0 && style.accelerate_after >= style.delay &&
        style.fast_interval > 0 && style.sound_interval > 0 && style.coarse_step > 0)) {
    throw std::runtime_error("level_control: invalid repeat timings or step");
  }
  return style;
}

int LevelControl::Repeat::step(const int wanted, const double seconds,
                               const LevelControlStyle &style) {
  if (wanted == 0) {
    *this = {};
    return 0;
  }
  if (direction != wanted) {
    direction = wanted;
    held = 0;
    next = style.delay;
    return wanted;
  }
  held += seconds;
  int count = 0;
  while (held + 1e-9 >= next) {
    ++count;
    next += held >= style.accelerate_after ? style.fast_interval : style.interval;
  }
  return direction * count;
}

void LevelControl::reset() {
  repeat_ = {};
  dragging_ = false;
  sound_wait_ = 0;
}

LevelChange LevelControl::update(const double seconds, const InputFrame &input, const int level,
                                 const int minimum, const int maximum, const Buttons left,
                                 const Buttons right, const LevelControlStyle &style,
                                 const LevelMeterHit &meter) {
  sound_wait_ = std::max(0.0, sound_wait_ - seconds);
  int value = level;
  if (!input.mouse_left_held) {
    dragging_ = false;
  }
  if (input.pointer && meter.step > 0) {
    const auto [x, y] = *input.pointer;
    const bool on_bar = x >= meter.zero_x + meter.step * minimum &&
                        x <= meter.zero_x + meter.step * maximum && y >= meter.top &&
                        y <= meter.bottom;
    if (input.clicked && on_bar) {
      dragging_ = true;
    }
    if (dragging_) {
      value = static_cast<int>(std::lround((x - meter.zero_x) / meter.step));
    }
  } else {
    dragging_ = false;
  }
  if (dragging_) {
    repeat_ = {};
  } else {
    const Buttons buttons = input.held | input.pressed;
    const auto direction = [](const Buttons mask, const Buttons back, const Buttons forward) {
      return int((mask & forward) != 0) - int((mask & back) != 0);
    };
    const int pad = direction(buttons, button::pad_left, button::pad_right);
    // Physical D-pad bindings also set the menu direction bits. Remove those
    // aliases before counting a separate keyboard/stick input.
    const Buttons independent = buttons & ~((buttons & button::pad) >> 8);
    const int stick = std::abs(input.left_stick[0]) > 0.5F ? (input.left_stick[0] > 0 ? 1 : -1)
                                                           : direction(independent, left, right);
    const int coarse = direction(buttons, style.coarse_back, style.coarse_forward);
    value += repeat_[0].step(pad, seconds, style) + repeat_[1].step(stick, seconds, style) +
             style.coarse_step * repeat_[2].step(coarse, seconds, style);
    const int end = direction(input.pressed, style.minimum, style.maximum);
    if (end != 0) {
      value = end > 0 ? maximum : minimum;
    }
  }
  value = std::clamp(value, minimum, maximum);
  const bool sound = value != level && sound_wait_ <= 0;
  if (sound) {
    sound_wait_ = style.sound_interval;
  }
  return {value, sound};
}

} // namespace phantomcrash
