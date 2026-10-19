#include "phantomcrash/movement_tuning.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace phantomcrash {
namespace {

float number(const json::JsonValue &value, const char *key, const float fallback) {
  return static_cast<float>(value.number_or(key, fallback));
}

} // namespace

std::optional<WheeledTuning> parse_wheeled_tuning(const json::JsonValue *value) {
  if (!value) {
    return std::nullopt;
  }
  const auto mode = value->string_or("mode", "walking");
  if (mode == "walking") {
    return std::nullopt;
  }
  if (mode != "wheeled") {
    throw std::runtime_error("Unknown leg movement mode: " + mode);
  }
  const auto positive = [&](const char *key) {
    const auto *field = value->find(key);
    if (!field || !field->is_number() || !std::isfinite(field->number()) || field->number() <= 0) {
      throw std::runtime_error(std::string("Wheeled movement needs a positive ") + key);
    }
    return static_cast<float>(field->number());
  };
  return WheeledTuning{positive("acceleration"),       positive("braking"),
                       positive("lateral_grip"),       positive("turn_scale"),
                       positive("reverse_turn_scale"), positive("radius")};
}

MovementTuning parse_movement_tuning(const json::JsonValue &value) {
  MovementTuning t;
  t.wheels = parse_wheeled_tuning(value.find("movement"));
  t.walk_speed = number(value, "walk_speed", t.walk_speed);
  t.back_speed = number(value, "back_speed", t.back_speed);
  t.walk_accel = number(value, "walk_accel", t.walk_accel);
  t.dash_speed = number(value, "dash_speed", t.dash_speed);
  t.dash_back_speed = number(value, "dash_back_speed", t.dash_back_speed);
  t.turn_degrees_per_second = number(value, "turn_degrees_per_second", t.turn_degrees_per_second);
  t.dash_turn_degrees_per_second =
      number(value, "dash_turn_degrees_per_second", t.dash_turn_degrees_per_second);
  t.air_turn_degrees_per_second =
      number(value, "air_turn_degrees_per_second", t.air_turn_degrees_per_second);
  t.stick_threshold = number(value, "stick_threshold", t.stick_threshold);
  t.dash_stick = number(value, "dash_stick", t.dash_stick);
  t.dash_hold_seconds = number(value, "dash_hold_seconds", t.dash_hold_seconds);
  t.brake_cut_seconds = number(value, "brake_cut_seconds", t.brake_cut_seconds);
  if (const auto *legs = value.find("dash_turn_legs")) {
    t.dash_turn_legs.clear();
    for (const auto &leg : legs->array()) {
      t.dash_turn_legs.push_back(static_cast<int>(leg.number()));
    }
  }
  t.dash_turn_stick = number(value, "dash_turn_stick", t.dash_turn_stick);
  t.dash_turn_ease_ticks =
      std::max(1.0F, number(value, "dash_turn_ease_ticks", t.dash_turn_ease_ticks));
  t.flick_stick = number(value, "flick_stick", t.flick_stick);
  t.standard_air_strafe = value.boolean_or("standard_air_strafe", t.standard_air_strafe);
  t.stop_keep = number(value, "stop_keep", t.stop_keep);
  t.stop_speed = number(value, "stop_speed", t.stop_speed);
  t.flick_seconds = number(value, "flick_seconds", t.flick_seconds);
  t.standard_air_turn_stick = number(value, "standard_air_turn_stick", t.standard_air_turn_stick);
  t.quick_turn_stick = number(value, "quick_turn_stick", t.quick_turn_stick);
  t.quick_turn_degrees = number(value, "quick_turn_degrees", t.quick_turn_degrees);
  t.quick_turn_share = number(value, "quick_turn_share", t.quick_turn_share);
  t.quick_turn_cooldown_seconds =
      number(value, "quick_turn_cooldown_seconds", t.quick_turn_cooldown_seconds);
  t.hit_seconds = number(value, "hit_seconds", t.hit_seconds);
  t.jump_height = std::max(0.0F, number(value, "jump_height", t.jump_height));
  t.down_boost_acceleration =
      std::max(0.0F, number(value, "down_boost_acceleration", t.down_boost_acceleration));
  t.gravity = number(value, "gravity", t.gravity);
  t.air_control = number(value, "air_control", t.air_control);
  t.dodge_speed = number(value, "dodge_speed", t.dodge_speed);
  t.air_dodge_speed = number(value, "air_dodge_speed", t.dodge_speed);
  t.dodge_keep = number(value, "dodge_keep", t.dodge_keep);
  t.dodge_ease = number(value, "dodge_ease", t.dodge_ease);
  t.air_dodge_keep = number(value, "air_dodge_keep", t.air_dodge_keep);
  t.dodge_seconds = number(value, "dodge_seconds", t.dodge_seconds);
  t.strike_speed = number(value, "strike_speed", t.strike_speed);
  t.lunge_rate_speed = number(value, "lunge_rate_speed", t.lunge_rate_speed);
  t.computer_lunge_rate_speed =
      number(value, "computer_lunge_rate_speed", t.computer_lunge_rate_speed);
  t.lunge_turn_degrees = number(value, "lunge_turn_degrees", t.lunge_turn_degrees);
  t.computer_lunge_turn_degrees =
      number(value, "computer_lunge_turn_degrees", t.computer_lunge_turn_degrees);
  t.lunge_cone_degrees = number(value, "lunge_cone_degrees", t.lunge_cone_degrees);
  t.lunge_climb_rate = number(value, "lunge_climb_rate", t.lunge_climb_rate);
  t.twist_degrees_per_second =
      number(value, "twist_degrees_per_second", t.twist_degrees_per_second);
  t.twist_limit_degrees = number(value, "twist_limit_degrees", t.twist_limit_degrees);
  t.twist_stretch_degrees = number(value, "twist_stretch_degrees", t.twist_stretch_degrees);
  t.twist_stick_keep = number(value, "twist_stick_keep", t.twist_stick_keep);
  t.twist_reset_seconds = number(value, "twist_reset_seconds", t.twist_reset_seconds);
  t.pitch_limit = number(value, "pitch_limit", t.pitch_limit);
  t.pitch_per_second = number(value, "pitch_per_second", t.pitch_per_second);
  return t;
}

} // namespace phantomcrash
