#pragma once

#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/part_stats.hpp"

#include <vector>

namespace phantomcrash {

class DrawList;

struct StatComparison {
  std::vector<StatLine> before;
  std::uint32_t increase{0x80dfff}, decrease{0xff6666};
  float scale{0.7F};
  float width{88.0F};
};

// The stats panel's values (0x03cbc3): one under the other from `top`,
// right-aligned on the column, the level beside HEAVY, LIGHT or NORMAL.
void draw_part_stats(DrawList &list, const GlyphFont &font, const PartStatsStyle &style,
                     const std::vector<StatLine> &lines, const Goods &goods, float top,
                     const StatComparison *comparison = nullptr);

} // namespace phantomcrash
