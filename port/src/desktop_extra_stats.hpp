#pragma once

#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/module_shop_script.hpp"
#include "phantomcrash/part_stats.hpp"
#include "phantomcrash/shop_list_style.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace phantomcrash {

class DrawList;

// A details window grown `extra` canvas pixels downward, its art's middle
// stretched between its grow_caps; as it is for none, or without them.
[[nodiscard]] PopWindow grown_window(const PopWindow &window, float extra);

// Extra stat lines `printed` lines below the first of `names` (the labels'
// art): each label and unit in the style's lettering, the value as the
// printed lines' are (from `values_top`), and the art's separator under it.
void draw_extra_stats(DrawList &list, const GlyphFont &font, const ExtraStatsStyle &style,
                      const std::vector<ExtraStatLine> &lines, const Sprite &names,
                      std::size_t printed, const PartStatsStyle &stats, float values_top);

} // namespace phantomcrash
