#include "desktop_location_menu.hpp"
#include "phantomcrash/shop_pointer.hpp"
#include <algorithm>
#include <cmath>

namespace phantomcrash {
int LocationMenuScreen::item_at(const InputFrame &input) {
  if (!input.pointer || (*input.pointer)[0] < 0 || (*input.pointer)[0] >= 640 ||
      (*input.pointer)[1] < 0 || (*input.pointer)[1] >= 480) {
    return -1;
  }
  const auto &script = menu_.script();
  const auto &selected = script.items[static_cast<std::size_t>(menu_.cursor())];
  if (!menu_.turning() &&
      (textures_.hit(selected.highlight, *input.pointer) || shop_hit(input, selected.label))) {
    return menu_.cursor();
  }
  auto wedges = menu_.wedges();
  for (auto it = wedges.rbegin(); it != wedges.rend(); ++it) {
    Sprite wedge = script.items[static_cast<std::size_t>(it->first)].wedge;
    wedge.position = script.wheel.position;
    wedge.pivot = script.wheel.pivot;
    wedge.angle = it->second;
    if (!textures_.hit(wedge, *input.pointer)) {
      continue;
    }
    // Fixed layers cover the wheel's hidden lower half and its transparent gaps.
    for (const auto &layer : script.layers) {
      if (layer.layer >= wedge.layer && textures_.hit(layer, *input.pointer)) {
        return -1;
      }
    }
    return it->first;
  }
  return -1;
}

void LocationMenuScreen::pointer_input(const InputFrame &input) {
  if (!menu_.faded_in() || menu_.done()) {
    return;
  }
  constexpr float wheel_pi = 3.14159265358979323846F;
  constexpr float drag_pixels = 8.0F;
  const auto &wheel = menu_.script().wheel;
  const auto angle_at = [&](const std::array<float, 2> &p) {
    return std::atan2(p[1] - wheel.position[1] - wheel.pivot[1],
                      p[0] - wheel.position[0] - wheel.pivot[0]);
  };
  if (input.pointer) {
    wheel_steps_ -= input.mouse_wheel;
  }
  if (input.clicked && input.pointer) {
    pressed_item_ = item_at(input);
    drag_start_ = pressed_item_ >= 0 ? input.pointer : std::nullopt;
    drag_angle_ = angle_at(*input.pointer);
    drag_turn_ = 0;
    drag_steps_ = 0;
    dragged_ = false;
  }
  if (drag_start_ && input.pointer) {
    const auto p = *input.pointer;
    const float angle = angle_at(p);
    drag_turn_ += std::remainder(angle - drag_angle_, 2 * wheel_pi);
    drag_angle_ = angle;
    dragged_ |= std::hypot(p[0] - (*drag_start_)[0], p[1] - (*drag_start_)[1]) >= drag_pixels;
    const float step = std::abs(wheel.step_degrees) * wheel_pi / 180;
    if (dragged_ && step > 0) {
      int steps = static_cast<int>(std::round(drag_turn_ / step));
      if (!input.mouse_left_held && steps == 0 && drag_steps_ == 0 &&
          std::abs(drag_turn_) > 0.01F) {
        steps = drag_turn_ > 0 ? 1 : -1;
      }
      wheel_steps_ -= static_cast<float>(steps - drag_steps_);
      drag_steps_ = steps;
    }
    if (!input.mouse_left_held) {
      if (!dragged_ && item_at(input) == pressed_item_) {
        SDL_Log("Shop wheel: clicked %s", menu_.script().items[pressed_item_].id.c_str());
        menu_.choose(pressed_item_);
      }
      drag_start_.reset();
    }
  } else if (!input.mouse_left_held) {
    drag_start_.reset();
  }
  if (!menu_.turning() && std::abs(wheel_steps_) >= 1) {
    const int direction = wheel_steps_ > 0 ? 1 : -1;
    menu_.rotate(direction);
    wheel_steps_ -= direction;
    SDL_Log("Shop wheel: rotation=%d item=%s", direction,
            menu_.script().items[menu_.cursor()].id.c_str());
  }
}
} // namespace phantomcrash
