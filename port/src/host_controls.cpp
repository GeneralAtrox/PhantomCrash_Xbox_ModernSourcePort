#include "phantomcrash/host_controls.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {

float host_stick_axis(const int raw, const float dead_zone) {
  const float axis = static_cast<float>(std::clamp(raw, -32767, 32767)) / 32767.0F;
  const float magnitude = std::max(0.0F, (std::abs(axis) - dead_zone) / (1.0F - dead_zone));
  return std::copysign(magnitude, axis);
}

std::vector<HostBinding> default_host_bindings() {
  return {{"up", "Move forward / menu up", {"W", "Up"}, "lefty-"},
          {"down", "Move backward / menu down", {"S", "Down"}, "lefty+"},
          {"left", "Strafe left / menu left", {"A", "Left"}, "leftx-"},
          {"right", "Strafe right / menu right", {"D", "Right"}, "leftx+"},
          {"aim_left", "Turn left / lesson torso twist", {"J"}, "rightx-"},
          {"aim_right", "Turn right / lesson torso twist", {"L"}, "rightx+"},
          {"aim_up", "Aim up", {"I"}, "righty-"},
          {"aim_down", "Aim down", {"K"}, "righty+"},
          {"left_trigger", "Fire left arm", {"Q"}, "lefttrigger+"},
          {"right_trigger", "Fire right arm", {"E"}, "righttrigger+"},
          {"y", "Fire left shoulder / Y", {"V"}, "y"},
          {"b", "Fire right shoulder / cancel", {"X", "Backspace"}, "b"},
          {"a", "Jump / airborne fast descent / confirm", {"Space", "Z"}, "a"},
          {"x", "Optic camouflage / X", {"C"}, "x"},
          {"white", "Dodge left / white", {"R"}, "leftshoulder"},
          {"black", "Dodge right / black", {"F"}, "rightshoulder"},
          {"left_thumb", "Dash / airborne fast descent", {"Left Shift"}, "leftstick"},
          {"right_thumb", "Reset torso / right stick click", {"T"}, "rightstick"},
          {"pad_up", "First-person camera / D-pad up", {"1"}, "dpup"},
          {"pad_down", "Behind camera / D-pad down", {"2"}, "dpdown"},
          {"pad_left", "Previous camera / D-pad left", {"3"}, "dpleft"},
          {"pad_right", "Next camera / D-pad right", {"4"}, "dpright"},
          {"start", "Start / confirm / pause battle", {"Return", "Keypad Enter"}, "start"},
          {"back", "Back / pause battle", {"Escape"}, "back"}};
}

HostControls host_controls_from_json(const json::JsonValue &value) {
  HostControls controls;
  controls.mouse_aim = value.boolean_or("mouse_aim", true);
  controls.invert_mouse_y = value.boolean_or("invert_mouse_y", false);
  const double dead_zone = value.number_or("stick_dead_zone", original_stick_dead_zone);
  if (std::isfinite(dead_zone)) {
    controls.stick_dead_zone = static_cast<float>(std::clamp(dead_zone, 0.0, 0.95));
  }
  const double sensitivity = value.number_or("mouse_degrees_per_pixel", 0.15);
  if (std::isfinite(sensitivity)) {
    controls.mouse_degrees_per_pixel = static_cast<float>(std::clamp(sensitivity, 0.01, 2.0));
  }
  if (const auto *bindings = value.find("bindings"); bindings && bindings->is_object()) {
    for (HostBinding &binding : controls.bindings) {
      const auto *saved = bindings->find(binding.action);
      if (!saved || !saved->is_object()) {
        continue;
      }
      binding.pad = saved->string_or("pad", binding.pad);
      if (const auto *keys = saved->find("keys"); keys && keys->is_array()) {
        binding.keys.clear();
        for (const auto &key : keys->array()) {
          binding.keys.push_back(key.string());
        }
      }
    }
  }
  return controls;
}

json::JsonValue host_controls_to_json(const HostControls &controls) {
  using json::JsonValue;
  JsonValue::Object bindings;
  for (const HostBinding &binding : controls.bindings) {
    JsonValue::Array keys;
    for (const std::string &key : binding.keys) {
      keys.emplace_back(key);
    }
    bindings.emplace_back(binding.action,
                          JsonValue{JsonValue::Object{{"keys", JsonValue{std::move(keys)}},
                                                      {"pad", JsonValue{binding.pad}}}});
  }
  return JsonValue{JsonValue::Object{
      {"mouse_aim", JsonValue{controls.mouse_aim}},
      {"invert_mouse_y", JsonValue{controls.invert_mouse_y}},
      {"stick_dead_zone", JsonValue{static_cast<double>(controls.stick_dead_zone)}},
      {"mouse_degrees_per_pixel", JsonValue{static_cast<double>(controls.mouse_degrees_per_pixel)}},
      {"bindings", JsonValue{std::move(bindings)}}}};
}

void bind_host_key(HostControls &controls, const std::string &action, const std::string &key) {
  for (HostBinding &binding : controls.bindings) {
    std::erase(binding.keys, key);
    if (binding.action == action) {
      binding.keys = key.empty() ? std::vector<std::string>{} : std::vector<std::string>{key};
    }
  }
}

void bind_host_pad(HostControls &controls, const std::string &action, const std::string &pad) {
  for (HostBinding &binding : controls.bindings) {
    if (binding.pad == pad) {
      binding.pad.clear();
    }
    if (binding.action == action) {
      binding.pad = pad;
    }
  }
}

} // namespace phantomcrash
