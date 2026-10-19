#include "phantomcrash/entry_shield.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>

namespace phantomcrash {

EntryShield parse_entry_shield(const json::JsonValue &value) {
  EntryShield shield;
  const auto number = [&](const char *key, const float fallback) {
    return static_cast<float>(value.number_or(key, fallback));
  };
  shield.seconds = number("seconds", shield.seconds);
  shield.fade_from = number("fade_from", shield.fade_from);
  shield.peak = number("peak", shield.peak);
  shield.rise_per_second = number("rise_per_second", shield.rise_per_second);
  shield.fall_per_second = number("fall_per_second", shield.fall_per_second);
  return shield;
}

EntryShieldLook parse_entry_shield_look(const json::JsonValue &value) {
  EntryShieldLook look;
  look.model = value.string_or("model", look.model);
  look.scale = static_cast<float>(value.number_or("scale", look.scale));
  look.spin_per_second =
      static_cast<float>(value.number_or("spin_per_second", look.spin_per_second));
  look.update_hz = std::max(1.0F, static_cast<float>(value.number_or("update_hz", look.update_hz)));
  look.phase_step = static_cast<float>(value.number_or("phase_step", look.phase_step));
  look.scroll_step = static_cast<float>(value.number_or("scroll_step", look.scroll_step));
  look.per_shield = value.boolean_or("per_shield", look.per_shield);
  if (const json::JsonValue *colour = value.find("colour")) {
    look.colour = json::numbers<4>(*colour, "entry_shield colour");
  }
  return look;
}

float entry_shield_glow(const EntryShield &shield, const float glow, const float left,
                        const bool alive, const float seconds) {
  // 0x082e60: rising while the shield lasts on a robot with armour left.
  const bool rising = left > shield.fade_from && alive;
  const float next = glow + (rising ? shield.rise_per_second : -shield.fall_per_second) * seconds;
  return std::clamp(next, 0.0F, shield.peak);
}

} // namespace phantomcrash
