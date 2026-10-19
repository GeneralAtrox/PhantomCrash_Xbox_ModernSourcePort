#pragma once

#include "phantomcrash/robot_parts.hpp"

namespace phantomcrash {

// Part record parsing shared by robot_parts.cpp: a level curve
// ({base, mid, top, knee, low_scale}) and a body's or legs' record.
[[nodiscard]] LevelCurve parse_level_curve(const json::JsonValue &entry);
[[nodiscard]] ArmourRecord parse_armour_record(const json::JsonValue &value);

} // namespace phantomcrash
