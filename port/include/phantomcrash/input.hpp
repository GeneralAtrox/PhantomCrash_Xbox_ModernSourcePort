#pragma once

#include "phantomcrash/json.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace phantomcrash {

// Original Xbox controller buttons as a bit set. Keyboard keys and modern
// gamepads are mapped onto these by the desktop layer.
using Buttons = std::uint32_t;

namespace button {
inline constexpr Buttons a = 1U << 0;
inline constexpr Buttons b = 1U << 1;
inline constexpr Buttons x = 1U << 2;
inline constexpr Buttons y = 1U << 3;
inline constexpr Buttons black = 1U << 4;
inline constexpr Buttons white = 1U << 5;
inline constexpr Buttons start = 1U << 6;
inline constexpr Buttons back = 1U << 7;
inline constexpr Buttons up = 1U << 8;
inline constexpr Buttons down = 1U << 9;
inline constexpr Buttons left = 1U << 10;
inline constexpr Buttons right = 1U << 11;
inline constexpr Buttons left_trigger = 1U << 12;
inline constexpr Buttons right_trigger = 1U << 13;
inline constexpr Buttons left_thumb = 1U << 14;
inline constexpr Buttons right_thumb = 1U << 15;
// The controller's D-pad itself, 8 bits above its direction: it also holds
// that direction, but the directions from the keys and the left stick do not
// hold these. The battle camera changes views with them.
inline constexpr Buttons pad_up = up << 8;
inline constexpr Buttons pad_down = down << 8;
inline constexpr Buttons pad_left = left << 8;
inline constexpr Buttons pad_right = right << 8;

// Groups the data files use. "confirm" is the original's A/START mask 0x110,
// "cancel" its B, and "any" every button.
inline constexpr Buttons confirm = a | start;
inline constexpr Buttons cancel = b | back;
inline constexpr Buttons directions = up | down | left | right;
inline constexpr Buttons any = (1U << 16) - 1;
inline constexpr Buttons pad = pad_up | pad_down | pad_left | pad_right;
} // namespace button

// The controller ports, one per VS player (0x0dee20 asks after each).
inline constexpr int controller_ports = 4;

// One port's own controller: whether one is connected, and its buttons.
struct PortInput {
  bool connected{};
  Buttons held{};
  Buttons pressed{};
  std::array<float, 2> left_stick{};
  std::array<float, 2> right_stick{};
};

// One logic tick's view of the controllers.
struct InputFrame {
  Buttons held{};
  // Buttons that went down since the previous tick, including taps that were
  // released again before the tick sampled them.
  Buttons pressed{};
  int controllers{};
  // The analog sticks, -1..1 with +x right and +y up. Without a stick
  // pushed, the held directions give the left stick full deflection.
  std::array<float, 2> left_stick{};
  std::array<float, 2> right_stick{};
  // Relative mouse aim in radians, consumed once per logic tick.
  std::array<float, 2> mouse_aim{};
  bool mouse_fire_left{};
  bool mouse_fire_right{};
  // The mouse in canvas units (the original's 640x480 space, after the
  // screen offset) while it is over the window, whether it moved since the
  // previous tick, and whether the left button went down.
  std::optional<std::array<float, 2>> pointer;
  bool pointer_moved{};
  bool clicked{};
  bool right_clicked{};    // right button down, consumed once (menu B action)
  bool mouse_right_held{}; // right button while the cursor is visible
  // Each port on its own; `held` and `pressed` above take them all together.
  std::array<PortInput, controller_ports> ports{};
  bool mouse_left_held{};
  bool mouse_double_clicked{};
  float mouse_wheel{};              // positive: scroll up; consumed once per logic tick
  std::optional<int> dialog_choice; // a visible YES/NO button clicked by the desktop
  // The keyboard's text while a screen takes it (Screen::wants_text): the
  // characters typed (UTF-8), Backspaces and Enter, in place of its buttons.
  std::string typed;
  int erased{};
  bool text_submit{};

  [[nodiscard]] bool any_pressed(Buttons mask) const {
    return (pressed & mask) != 0;
  }
};

// Port `port`'s buttons alone, as a frame of their own (no sticks or mouse).
[[nodiscard]] InputFrame port_frame(const InputFrame &frame, int port);

// Builds InputFrames from sampled button levels plus presses latched from
// events between ticks.
class InputEdges final {
public:
  InputFrame update(Buttons held, Buttons latched_presses, int controllers);

private:
  Buttons previous_{};
};

// A button or group name ("a", "start", "confirm", "cancel", "any", ...).
[[nodiscard]] std::optional<Buttons> button_from_name(std::string_view name);
// A name or an array of names from a data file; throws on an unknown name.
[[nodiscard]] Buttons buttons_from_json(const json::JsonValue &value);

} // namespace phantomcrash
