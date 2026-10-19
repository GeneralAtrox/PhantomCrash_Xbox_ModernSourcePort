#include "desktop_input.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {
constexpr float trigger_dead_zone = 0.2F;

// The stick's other axis, for a stick axis.
SDL_GamepadAxis across(const SDL_GamepadAxis axis) {
  switch (axis) {
  case SDL_GAMEPAD_AXIS_LEFTX:
    return SDL_GAMEPAD_AXIS_LEFTY;
  case SDL_GAMEPAD_AXIS_LEFTY:
    return SDL_GAMEPAD_AXIS_LEFTX;
  case SDL_GAMEPAD_AXIS_RIGHTX:
    return SDL_GAMEPAD_AXIS_RIGHTY;
  case SDL_GAMEPAD_AXIS_RIGHTY:
    return SDL_GAMEPAD_AXIS_RIGHTX;
  default:
    return SDL_GAMEPAD_AXIS_INVALID;
  }
}

Buttons action_mask(const std::string &action) {
  const Buttons mask = button_from_name(action).value_or(0);
  return mask | ((mask & button::pad) >> 8);
}
} // namespace

void DesktopInput::configure(const HostControls &controls) {
  if (controls_ && *controls_ == controls) {
    return;
  }
  controls_ = controls;
  bindings_.clear();
  for (const HostBinding &source : controls.bindings) {
    Binding binding;
    binding.action = source.action;
    binding.mask = action_mask(source.action);
    for (const std::string &name : source.keys) {
      const SDL_Scancode code = SDL_GetScancodeFromName(name.c_str());
      if (code != SDL_SCANCODE_UNKNOWN) {
        binding.keys.push_back(code);
      }
    }
    std::string physical = source.pad;
    if (physical.ends_with('+') || physical.ends_with('-')) {
      binding.sign = physical.back() == '-' ? -1.0F : 1.0F;
      physical.pop_back();
      binding.axis = SDL_GetGamepadAxisFromString(physical.c_str());
    } else {
      binding.button = SDL_GetGamepadButtonFromString(physical.c_str());
    }
    bindings_.push_back(std::move(binding));
  }
  latched_ = {};
  previous_ = {};
}

Buttons DesktopInput::key_button(const SDL_Scancode key) const {
  // Typing, the letters, digits, punctuation, space, Backspace and Enter
  // keys press nothing; Escape, Tab and the arrows still do.
  const bool types = (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_SLASH &&
                      key != SDL_SCANCODE_ESCAPE && key != SDL_SCANCODE_TAB) ||
                     (key >= SDL_SCANCODE_KP_DIVIDE && key <= SDL_SCANCODE_KP_PERIOD);
  if (text_mode_ && types) {
    return 0;
  }
  Buttons mask{};
  for (const Binding &binding : bindings_) {
    if (std::ranges::find(binding.keys, key) != binding.keys.end()) {
      mask |= binding.mask;
    }
  }
  return mask;
}

Buttons DesktopInput::pad_button(const Uint8 button) const {
  Buttons mask{};
  for (const Binding &binding : bindings_) {
    if (binding.button == button) {
      mask |= binding.mask;
    }
  }
  return mask;
}

float DesktopInput::value(const Binding &binding, SDL_Gamepad *pad) const {
  if (binding.button != SDL_GAMEPAD_BUTTON_INVALID) {
    return SDL_GetGamepadButton(pad, binding.button) ? 1.0F : 0.0F;
  }
  if (binding.axis != SDL_GAMEPAD_AXIS_INVALID) {
    const int raw = SDL_GetGamepadAxis(pad, binding.axis);
    if (across(binding.axis) != SDL_GAMEPAD_AXIS_INVALID) {
      const float dead_zone = controls_ ? controls_->stick_dead_zone : original_stick_dead_zone;
      return std::max(0.0F, host_stick_axis(raw, dead_zone) * binding.sign);
    }
    const float axis = static_cast<float>(raw) / 32767.0F;
    return std::clamp((axis * binding.sign - trigger_dead_zone) / (1.0F - trigger_dead_zone), 0.0F,
                      1.0F);
  }
  return 0.0F;
}

Buttons DesktopInput::pad_held(SDL_Gamepad *pad) const {
  Buttons mask{};
  for (const Binding &binding : bindings_) {
    // A stick pushed mostly one way is only that way as a button, so a menu
    // flicked up a little off true does not also step sideways.
    if (const SDL_GamepadAxis other = across(binding.axis);
        other != SDL_GAMEPAD_AXIS_INVALID && std::abs(SDL_GetGamepadAxis(pad, other)) >
                                                 std::abs(SDL_GetGamepadAxis(pad, binding.axis))) {
      continue;
    }
    if (value(binding, pad) > 0.5F) {
      mask |= binding.mask;
    }
  }
  return mask;
}

void DesktopInput::sticks(const Buttons held, InputFrame &frame, const bool focused,
                          const int port) const {
  int count{};
  const bool *keyboard = SDL_GetKeyboardState(&count);
  for (const Binding &binding : bindings_) {
    if (!focused) {
      continue;
    }
    float strength{};
    if (port <= 0 && SDL_GetKeyboardFocus() != nullptr && !(SDL_GetModState() & SDL_KMOD_ALT)) {
      for (const SDL_Scancode key : binding.keys) {
        if (key < count && keyboard[key]) {
          strength = 1.0F;
        }
      }
    }
    for (std::size_t i = 0; i < gamepads_.size(); ++i) {
      if (port < 0 || port_of(i) == port) {
        strength = std::max(strength, value(binding, gamepads_[i]));
      }
    }
    const auto &action = binding.action;
    if (action == "up") {
      frame.left_stick[1] += strength;
    }
    if (action == "down") {
      frame.left_stick[1] -= strength;
    }
    if (action == "left") {
      frame.left_stick[0] -= strength;
    }
    if (action == "right") {
      frame.left_stick[0] += strength;
    }
    if (action == "aim_up") {
      frame.right_stick[1] += strength;
    }
    if (action == "aim_down") {
      frame.right_stick[1] -= strength;
    }
    if (action == "aim_left") {
      frame.right_stick[0] -= strength;
    }
    if (action == "aim_right") {
      frame.right_stick[0] += strength;
    }
  }
  if (frame.left_stick == std::array<float, 2>{}) {
    const Buttons moving = held & ~((held & button::pad) >> 8);
    const auto axis = [&](const Buttons minus, const Buttons plus) {
      return ((moving & plus) != 0 ? 1.0F : 0.0F) - ((moving & minus) != 0 ? 1.0F : 0.0F);
    };
    frame.left_stick = {axis(button::left, button::right), axis(button::down, button::up)};
  }
}

void DesktopInput::capture_mouse(SDL_Window *window, const bool gameplay, const bool window_ui) {
  if (!gameplay && mouse_gameplay_) {
    mouse_released_ = false;
  }
  mouse_gameplay_ = gameplay;
  mouse_window_ = window;
  mouse_window_ui_ = window_ui;
  const bool capture = gameplay && controls_ && controls_->mouse_aim && !mouse_released_ &&
                       !window_ui && !(SDL_GetModState() & SDL_KMOD_ALT);
  if (capture != mouse_captured_) {
    mouse_captured_ = capture && SDL_SetWindowRelativeMouseMode(window, true);
    if (!capture) {
      SDL_SetWindowRelativeMouseMode(window, false);
    }
    mouse_delta_ = {};
    mouse_pressed_ = 0;
    suppress_mouse_fire_ = true; // A menu click must be released before it can fire.
    if (!capture) {
      SDL_ShowCursor();
    }
  }
}

} // namespace phantomcrash
