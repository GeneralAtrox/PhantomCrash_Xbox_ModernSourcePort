#include "desktop_input.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace phantomcrash {
namespace {

std::array<float, 2> render_pixels(const SDL_WindowID window, const float x, const float y) {
  std::array<float, 2> pixels{x, y};
  if (SDL_Renderer *renderer = SDL_GetRenderer(SDL_GetWindowFromID(window))) {
    SDL_RenderCoordinatesFromWindow(renderer, x, y, &pixels[0], &pixels[1]);
  }
  return pixels;
}

} // namespace

DesktopInput::~DesktopInput() {
  for (auto *pad : gamepads_) {
    SDL_RumbleGamepad(pad, 0, 0, 0);
    SDL_CloseGamepad(pad);
  }
}

void DesktopInput::handle(const SDL_Event &event) {
  switch (event.type) {
  case SDL_EVENT_KEY_UP:
    if (event.key.scancode == SDL_SCANCODE_LALT || event.key.scancode == SDL_SCANCODE_RALT) {
      capture_mouse(mouse_window_, mouse_gameplay_, mouse_window_ui_);
    }
    break;
  case SDL_EVENT_KEY_DOWN:
    if (event.key.scancode == SDL_SCANCODE_LALT || event.key.scancode == SDL_SCANCODE_RALT) {
      capture_mouse(mouse_window_, mouse_gameplay_, mouse_window_ui_);
      break;
    }
    if (event.key.scancode == SDL_SCANCODE_F10 && !event.key.repeat) {
      mouse_released_ = !mouse_released_;
      capture_mouse(mouse_window_, mouse_gameplay_, mouse_window_ui_);
      break;
    }
    if (text_mode_ && event.key.scancode == SDL_SCANCODE_BACKSPACE) {
      ++erased_; // held, it repeats
      break;
    }
    if (text_mode_ && !event.key.repeat && !(event.key.mod & SDL_KMOD_ALT) &&
        (event.key.scancode == SDL_SCANCODE_RETURN ||
         event.key.scancode == SDL_SCANCODE_KP_ENTER)) {
      text_submit_ = true;
      break;
    }
    // Alt combinations belong to the window (Alt+Enter, Alt+F4).
    if (!event.key.repeat && !(event.key.mod & SDL_KMOD_ALT)) {
      latched_[0] |= key_button(event.key.scancode);
    }
    break;
  case SDL_EVENT_TEXT_INPUT:
    if (text_mode_) {
      typed_ += event.text.text;
    }
    break;
  case SDL_EVENT_MOUSE_MOTION:
    if (mouse_captured_) {
      mouse_delta_[0] += event.motion.xrel;
      mouse_delta_[1] += event.motion.yrel;
    }
    pointer_pixels_ = render_pixels(event.motion.windowID, event.motion.x, event.motion.y);
    pointer_moved_ = true;
    break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if (mouse_gameplay_ && (mouse_window_ui_ || (SDL_GetModState() & SDL_KMOD_ALT))) {
      suppress_mouse_fire_ = true;
      break;
    }
    if (mouse_gameplay_ && mouse_released_) {
      mouse_released_ = false;
      suppress_mouse_fire_ = true;
      capture_mouse(mouse_window_, true, mouse_window_ui_);
      break;
    }
    if (mouse_captured_) {
      mouse_pressed_ |= SDL_BUTTON_MASK(event.button.button);
      break;
    }
    if (event.button.button == SDL_BUTTON_LEFT) {
      pointer_pixels_ = render_pixels(event.button.windowID, event.button.x, event.button.y);
      clicked_ = true;
      double_clicked_ |= event.button.clicks == 2;
    }
    if (event.button.button == SDL_BUTTON_RIGHT) {
      right_clicked_ = true;
    }
    break;
  case SDL_EVENT_MOUSE_WHEEL:
    if (!mouse_captured_) {
      pointer_pixels_ =
          render_pixels(event.wheel.windowID, event.wheel.mouse_x, event.wheel.mouse_y);
      mouse_wheel_ +=
          event.wheel.y * (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F);
    }
    break;
  case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    pointer_pixels_.reset();
    break;
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
    const auto found = std::ranges::find_if(
        gamepads_, [&](SDL_Gamepad *pad) { return SDL_GetGamepadID(pad) == event.gbutton.which; });
    const auto pad = static_cast<std::size_t>(found - gamepads_.begin());
    latched_[static_cast<std::size_t>(found != gamepads_.end() ? port_of(pad) : 0)] |=
        pad_button(event.gbutton.button);
    break;
  }
  case SDL_EVENT_GAMEPAD_ADDED:
    if (gamepads_.size() >= controller_ports) {
      break;
    }
    if (SDL_Gamepad *pad = SDL_OpenGamepad(event.gdevice.which)) {
      for (const int port : {1, 2, 3, 0}) {
        if (std::ranges::find(gamepad_ports_, port) == gamepad_ports_.end()) {
          gamepad_ports_.push_back(port);
          break;
        }
      }
      gamepads_.push_back(pad);
      rumbling_.emplace_back();
    }
    break;
  case SDL_EVENT_GAMEPAD_REMOVED: {
    const auto found = std::ranges::find_if(
        gamepads_, [&](SDL_Gamepad *pad) { return SDL_GetGamepadID(pad) == event.gdevice.which; });
    if (found != gamepads_.end()) {
      SDL_CloseGamepad(*found);
      rumbling_.erase(rumbling_.begin() + (found - gamepads_.begin()));
      gamepad_ports_.erase(gamepad_ports_.begin() + (found - gamepads_.begin()));
      gamepads_.erase(found);
    }
    break;
  }
  default:
    break;
  }
}

void DesktopInput::press(const Buttons buttons, const int port) {
  const auto at = static_cast<std::size_t>(std::clamp(port, 0, controller_ports - 1));
  injected_[at] |= buttons;
  scripted_ports_[at] = true;
}

void DesktopInput::hold(const Buttons buttons, const int port) {
  const auto at = static_cast<std::size_t>(std::clamp(port, 0, controller_ports - 1));
  injected_held_[at] |= buttons;
  scripted_ports_[at] = true;
}

void DesktopInput::rumble(const std::array<RumbleMotors, controller_ports> &motors,
                          const bool shared_controls) {
  // Each send lasts this long, and is renewed halfway, so a pad the game
  // stops talking to stops by itself.
  constexpr Uint32 rumble_ms = 1000;
  const Uint64 now = SDL_GetTicks();
  const auto speed = [](const float motor) {
    return static_cast<Uint16>(std::clamp(motor, 0.0F, 1.0F) * 65535.0F + 0.5F);
  };
  for (std::size_t pad = 0; pad < gamepads_.size(); ++pad) {
    const RumbleMotors &own = motors[static_cast<std::size_t>(port_of(pad))];
    const RumbleMotors wanted = shared_controls
                                    ? RumbleMotors{std::max(own.low, motors.front().low),
                                                   std::max(own.high, motors.front().high)}
                                    : own;
    PadRumble &last = rumbling_[pad];
    const bool running = wanted != RumbleMotors{};
    if (wanted == last.motors && !(running && now - last.sent_ms >= rumble_ms / 2)) {
      continue;
    }
    SDL_RumbleGamepad(gamepads_[pad], speed(wanted.low), speed(wanted.high), rumble_ms);
    last = {wanted, now};
  }
}

int DesktopInput::port_of(const std::size_t pad) const {
  return gamepad_ports_.at(pad);
}

DesktopInput::PortButtons DesktopInput::held_buttons() const {
  PortButtons held{};
  int count = 0;
  const bool *keyboard = SDL_GetKeyboardState(&count);
  if (SDL_GetModState() & SDL_KMOD_ALT) {
    keyboard = nullptr;
  }
  for (int scancode = 0; keyboard != nullptr && scancode < count; ++scancode) {
    if (keyboard[scancode]) {
      held[0] |= key_button(static_cast<SDL_Scancode>(scancode));
    }
  }
  for (std::size_t index = 0; index < gamepads_.size(); ++index) {
    Buttons &port = held[static_cast<std::size_t>(port_of(index))];
    port |= pad_held(gamepads_[index]);
  }
  return held;
}

// Each port's buttons and presses: the keyboard's port is always connected,
// the others while a gamepad is on them (or a scripted run presses them).
void DesktopInput::fill_ports(const bool focused, const PortButtons &held, InputFrame &frame) {
  for (std::size_t port = 0; port < frame.ports.size(); ++port) {
    PortInput &own = frame.ports[port];
    const Buttons latched = (focused ? latched_[port] : 0) | std::exchange(injected_[port], 0);
    own.held = held[port];
    own.pressed = latched | (held[port] & ~previous_[port]);
    previous_[port] = held[port];
    own.connected = port == 0 || scripted_ports_[port];
  }
  for (std::size_t index = 0; index < gamepads_.size(); ++index) {
    frame.ports[static_cast<std::size_t>(port_of(index))].connected = true;
  }
  latched_ = {};
}

InputFrame DesktopInput::sample(const bool focused, const Viewport &viewport) {
  PortButtons ports = focused ? held_buttons() : PortButtons{};
  Buttons held = 0;
  Buttons latched = 0;
  for (std::size_t port = 0; port < ports.size(); ++port) {
    ports[port] |= std::exchange(injected_held_[port], 0);
    held |= ports[port];
    // Injected presses do not need focus: smoke runs are hidden.
    latched |= (focused ? latched_[port] : 0) | injected_[port];
  }
  InputFrame frame = edges_.update(held, latched, controllers());
  frame.right_clicked = std::exchange(right_clicked_, false) && focused;
  frame.mouse_right_held = focused && (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_RMASK);
  frame.mouse_left_held = focused && (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK);
  if (focused && mouse_captured_ && controls_) {
    constexpr float radians_per_degree = 0.01745329252F;
    const float sensitivity = controls_->mouse_degrees_per_pixel * radians_per_degree;
    frame.mouse_aim = {mouse_delta_[0] * sensitivity,
                       mouse_delta_[1] * sensitivity * (controls_->invert_mouse_y ? 1.0F : -1.0F)};
    const Uint32 buttons = SDL_GetMouseState(nullptr, nullptr);
    if (suppress_mouse_fire_) {
      suppress_mouse_fire_ = buttons != 0;
    } else {
      frame.mouse_fire_left = ((buttons | mouse_pressed_) & SDL_BUTTON_LMASK) != 0;
      frame.mouse_fire_right = ((buttons | mouse_pressed_) & SDL_BUTTON_RMASK) != 0;
    }
  }
  mouse_delta_ = {};
  mouse_pressed_ = 0;
  fill_ports(focused, ports, frame);
  for (int port = 0; port < controller_ports; ++port) {
    InputFrame own;
    sticks(ports[port], own, focused, port);
    frame.ports[port].left_stick = own.left_stick;
    frame.ports[port].right_stick = own.right_stick;
  }
  if (focused) {
    sticks(held, frame, true);
  } else {
    sticks(held & button::directions, frame, false); // scripted runs hold directions
    frame.right_stick = {};
  }
  if (scripted_pointer_) {
    frame.pointer = scripted_pointer_;
    frame.pointer_moved = std::exchange(scripted_moved_, false);
    frame.clicked = std::exchange(scripted_click_, false);
  } else if (focused && pointer_pixels_) {
    const auto [x, y] = *pointer_pixels_;
    frame.pointer = std::array<float, 2>{(x - viewport.offset_x) / viewport.scale,
                                         (y - viewport.offset_y) / viewport.scale};
    frame.pointer_moved = pointer_moved_;
    frame.clicked = clicked_;
    frame.mouse_double_clicked = double_clicked_;
  }
  frame.mouse_wheel = std::exchange(mouse_wheel_, 0.0F);
  if (!focused) {
    frame.mouse_wheel = 0;
  }
  pointer_moved_ = false;
  clicked_ = false;
  double_clicked_ = false;
  if (focused) {
    frame.typed = std::move(typed_);
    frame.erased = erased_;
    frame.text_submit = text_submit_;
  }
  typed_.clear();
  erased_ = 0;
  text_submit_ = false;
  return frame;
}

void DesktopInput::set_text_mode(SDL_Window *window, const bool on) {
  if (on == text_mode_ || window == nullptr) {
    return;
  }
  text_mode_ = on;
  if (on) {
    SDL_StartTextInput(window);
  } else {
    SDL_StopTextInput(window);
  }
  // Keys held across the switch must not type or press.
  typed_.clear();
  erased_ = 0;
  text_submit_ = false;
  latched_ = {};
}

bool is_fullscreen_toggle(const SDL_Event &event) {
  if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) {
    return false;
  }
  const SDL_Scancode key = event.key.scancode;
  return key == SDL_SCANCODE_F11 || ((key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER) &&
                                     (event.key.mod & SDL_KMOD_ALT));
}

} // namespace phantomcrash
