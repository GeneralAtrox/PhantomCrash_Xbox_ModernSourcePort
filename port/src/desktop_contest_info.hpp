#pragma once

#include "phantomcrash/contest_info.hpp"
#include "phantomcrash/contests.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/location_frame.hpp"

#include <array>
#include <cstdint>

namespace phantomcrash {

class DrawList;

// An amount of NY as the HUD writes money (0x0bc769, commas), its left edge
// at `at` and `unit` one past its right.
void draw_amount(DrawList &list, const NumberFont &font, const std::array<float, 2> &at, int digits,
                 std::int64_t amount, Sprite unit, std::uint32_t colour, int layer);

// A contest's details in `info`'s places (its panel is the caller's): the
// schedule's popup and RUMBLING's CONTEST INFO.
void draw_contest_info(DrawList &list, const GlyphFont &font, const NumberFont &fee_font,
                       const ContestInfoStyle &info, const ContestTables &contests, int contest);

} // namespace phantomcrash
