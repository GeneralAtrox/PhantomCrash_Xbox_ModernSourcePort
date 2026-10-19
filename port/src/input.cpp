#include "phantomcrash/input.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace phantomcrash {
namespace {

constexpr std::array<std::pair<std::string_view, Buttons>, 25> names{{
    {"a", button::a},
    {"b", button::b},
    {"x", button::x},
    {"y", button::y},
    {"black", button::black},
    {"white", button::white},
    {"start", button::start},
    {"back", button::back},
    {"up", button::up},
    {"down", button::down},
    {"left", button::left},
    {"right", button::right},
    {"left_trigger", button::left_trigger},
    {"right_trigger", button::right_trigger},
    {"left_thumb", button::left_thumb},
    {"right_thumb", button::right_thumb},
    {"pad_up", button::pad_up},
    {"pad_down", button::pad_down},
    {"pad_left", button::pad_left},
    {"pad_right", button::pad_right},
    {"confirm", button::confirm},
    {"cancel", button::cancel},
    {"directions", button::directions},
    {"any", button::any},
    {"none", Buttons{}},
}};

} // namespace

InputFrame InputEdges::update(const Buttons held, const Buttons latched_presses,
                              const int controllers) {
  InputFrame frame{held, latched_presses | (held & ~previous_), controllers};
  previous_ = held;
  return frame;
}

InputFrame port_frame(const InputFrame &frame, const int port) {
  if (port < 0 || port >= controller_ports) {
    return {};
  }
  const PortInput &own = frame.ports[static_cast<std::size_t>(port)];
  InputFrame out{own.held, own.pressed, own.connected ? 1 : 0};
  out.left_stick = own.left_stick;
  out.right_stick = own.right_stick;
  out.ports[0] = own;
  return out;
}

std::optional<Buttons> button_from_name(const std::string_view name) {
  for (const auto &[candidate, mask] : names) {
    if (candidate == name) {
      return mask;
    }
  }
  return std::nullopt;
}

Buttons buttons_from_json(const json::JsonValue &value) {
  const auto one = [](const json::JsonValue &item) {
    const auto &name = item.string();
    if (auto mask = button_from_name(name)) {
      return *mask;
    }
    throw std::runtime_error("Unknown button name: " + name);
  };
  if (value.is_array()) {
    Buttons result{};
    for (const auto &item : value.array()) {
      result |= one(item);
    }
    return result;
  }
  return one(value);
}

} // namespace phantomcrash
