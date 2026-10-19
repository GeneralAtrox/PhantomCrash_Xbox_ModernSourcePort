#pragma once

#include "phantomcrash/json.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

struct HostBinding {
  std::string action;
  std::string label;
  std::vector<std::string> keys;
  std::string pad;
  bool operator==(const HostBinding &) const = default;
};

[[nodiscard]] std::vector<HostBinding> default_host_bindings();

// 0x0dff50: each stick axis discards +/-0x4000, then rescales to +/-0x7fff.
inline constexpr float original_stick_dead_zone = 16384.0F / 32767.0F;
[[nodiscard]] float host_stick_axis(int raw, float dead_zone = original_stick_dead_zone);

struct HostControls {
  std::vector<HostBinding> bindings{default_host_bindings()};
  bool mouse_aim{true};
  bool invert_mouse_y{};
  float mouse_degrees_per_pixel{0.15F};
  float stick_dead_zone{original_stick_dead_zone};
  bool operator==(const HostControls &) const = default;
};

[[nodiscard]] HostControls host_controls_from_json(const json::JsonValue &value);
[[nodiscard]] json::JsonValue host_controls_to_json(const HostControls &controls);
// Assign a physical input to one action, removing its previous assignment.
void bind_host_key(HostControls &controls, const std::string &action, const std::string &key);
void bind_host_pad(HostControls &controls, const std::string &action, const std::string &pad);

} // namespace phantomcrash
