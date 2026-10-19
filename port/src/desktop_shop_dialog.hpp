#pragma once

#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/shop_dialog.hpp"

#include <cstdint>
#include <string>

namespace phantomcrash {

class DrawList;

// A shop's dialog saying `saying` (a key of its "says"): the window, the
// sprites over it, the clerk's face and the lines, "{price}" filled in.
// Nothing when it has nothing to say for `saying`.
void draw_shop_dialog(DrawList &list, const GlyphFont &font, const ShopDialog &dialog,
                      const std::string &saying, std::int64_t price);

// A block of lines, one under the other, with its alpha.
void draw_text_block(DrawList &list, const GlyphFont &font, const TextBlock &block,
                     float alpha = 1.0F);

} // namespace phantomcrash
