#pragma once

#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/shop_goods.hpp"
#include "phantomcrash/shop_list_style.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <cstdint>
#include <vector>

namespace phantomcrash {

class DrawList;

// A window popping open (0x03c6fe) `seconds` after it began; true once it
// is fully open.
bool draw_pop_window(DrawList &list, const PopWindow &pop, double seconds);

// `sprite` moved by (dx, dy).
[[nodiscard]] Sprite shifted(Sprite sprite, float dx, float dy);

// A goods list's rows (0x1f8a3, 0x21cd2, 0x303d7): the arrows when there is
// more above or below, and for each line shown its name, its maker's icon
// and, for the player's own, the hangar it is fitted in (or the mark of a
// paint or emblem worn) and which of a pair of weapon slots.
// How a row reads where the list says more than the shops' do (ASSEMBLE,
// 0x019144): its name's colour, its hangar badge's, and whether it is
// fitted in the hangar assembled.
struct GoodsRowLook {
  std::uint32_t name{0xffffff};
  std::uint32_t badge{0xffffff};
  bool here{};
};

void draw_goods_rows(DrawList &list, const GlyphFont &font, const ShopGoodsList &style,
                     const GoodsList &goods, const std::vector<GoodsRowLook> &looks = {});

// One row `dy` below the first, by `look` when there is one.
void draw_goods_row(DrawList &list, const GlyphFont &font, const ShopGoodsList &style,
                    const ShopGoods &line, const GoodsRowLook *look, float dy);

} // namespace phantomcrash
