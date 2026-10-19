#pragma once

// A contest's details as the schedule's popup (0x01469d) and RUMBLING's
// CONTEST INFO (0x013d07) show them: its name, its class, each regulation
// the class has (its regulation text) or "No limits.", and the fee in NY.

#include "phantomcrash/json.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/town_screen.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace phantomcrash {

struct ContestInfoStyle {
  Sprite panel;
  PopupGrowth growth;
  std::array<float, 2> contest{}, body{}, legs{}, other{}, fee{};
  std::optional<std::array<float, 2>> contest_class; // none: the class is not named
  int fee_digits{9};
  Sprite fee_unit;
  std::string no_limits;
  std::uint32_t text_colour{0xffffff};
  int text_layer{0x109};
};

// "panel" (optional), the growth, the lines' places ("class" optional),
// "fee_digits", "fee_unit", "no_limits", "text_colour" and "text_layer".
// Throws std::runtime_error naming the member at fault.
[[nodiscard]] ContestInfoStyle parse_contest_info(const json::JsonValue &value);

} // namespace phantomcrash
