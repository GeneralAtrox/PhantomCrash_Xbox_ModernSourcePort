#include "desktop_shop_list.hpp"

#include "desktop_draw_list.hpp"
#include "phantomcrash/town_screen.hpp"

#include <algorithm>
#include <array>

namespace phantomcrash {
namespace {

// The sprite at `index` of `sprites`, moved down `dy`, when there is one.
void draw_indexed(DrawList &list, const std::vector<Sprite> &sprites, const int index,
                  const float dy) {
  if (index >= 0 && index < static_cast<int>(sprites.size())) {
    list.sprite(shifted(sprites[static_cast<std::size_t>(index)], 0.0F, dy));
  }
}

} // namespace

bool draw_pop_window(DrawList &list, const PopWindow &pop, const double seconds) {
  const float t = static_cast<float>(std::clamp(seconds / pop.seconds, 0.0, 1.0));
  if (t <= 0.0F) {
    return false;
  }
  const auto scale = pop_open(t, 1.0F, pop.thin);
  Sprite window = pop.sprite;
  const std::array<float, 2> full = window.size.value_or(
      std::array{window.rect[2] - window.rect[0], window.rect[3] - window.rect[1]});
  window.centered = true;
  window.size = std::array<float, 2>{full[0] * scale[0], full[1] * scale[1]};
  list.sprite(window);
  if (pop.caps) {
    // The caps keep to the middle's edges, above and below it.
    for (std::size_t end = 0; end < 2; ++end) {
      Sprite cap = (*pop.caps)[end];
      const float height = (cap.rect[3] - cap.rect[1]) * scale[1];
      const float edge = window.size->at(1) / 2.0F + height / 2.0F;
      cap.centered = true;
      cap.size = std::array<float, 2>{(cap.rect[2] - cap.rect[0]) * scale[0], height};
      cap.position = {window.position[0], window.position[1] + (end == 0 ? -edge : edge)};
      list.sprite(cap);
    }
  }
  return t >= 1.0F;
}

Sprite shifted(Sprite sprite, const float dx, const float dy) {
  sprite.position[0] += dx;
  sprite.position[1] += dy;
  return sprite;
}

void draw_goods_rows(DrawList &list, const GlyphFont &font, const ShopGoodsList &style,
                     const GoodsList &goods, const std::vector<GoodsRowLook> &looks) {
  const auto &lines = goods.goods();
  const int count = static_cast<int>(lines.size());
  if (style.row_plate) {
    for (int row = 0; row < style.rows; ++row) {
      list.sprite(shifted(*style.row_plate, 0.0F, style.row_step * static_cast<float>(row)));
    }
  }
  if (count > style.rows) {
    if (goods.scroll() > 0) {
      list.sprite(style.scroll[0]);
    }
    if (goods.scroll() + style.rows < count) {
      list.sprite(style.scroll[1]);
    }
  }
  for (int row = 0; row < style.rows && goods.scroll() + row < count; ++row) {
    const auto index = static_cast<std::size_t>(goods.scroll() + row);
    const ShopGoods &line = lines[index];
    const GoodsRowLook *look = index < looks.size() ? &looks[index] : nullptr;
    draw_goods_row(list, font, style, line, look, style.row_step * static_cast<float>(row));
  }
}

void draw_goods_row(DrawList &list, const GlyphFont &font, const ShopGoodsList &style,
                    const ShopGoods &line, const GoodsRowLook *look, const float dy) {
  const float width = goods_name_width(style, font.cell);
  list.text(style.name_layer,
            layout_label(font, line.name, style.name_at[0], style.name_at[1] + dy, width),
            look != nullptr ? look->name : style.name_colour);
  draw_indexed(list, style.maker_icons, line.maker, dy);
  if (line.in_use && look == nullptr) {
    list.sprite(shifted(style.in_use, 0.0F, dy));
    return;
  }
  if (style.badge_base && line.goods.kind != GoodsKind::paint &&
      line.goods.kind != GoodsKind::emblem) {
    list.sprite(shifted(*style.badge_base, 0.0F, dy));
  }
  if (line.hangar < 0) {
    return;
  }
  if (line.hangar < static_cast<int>(style.hangar_badges.size())) {
    for (Sprite badge : style.hangar_badges[static_cast<std::size_t>(line.hangar)]) {
      badge = shifted(badge, 0.0F, dy);
      if (look != nullptr) {
        badge.colour = look->badge;
      }
      list.sprite(badge);
    }
  }
  if (line.side < 0 || line.side >= static_cast<int>(style.side_marks.size())) {
    return;
  }
  Sprite mark = shifted(style.side_marks[static_cast<std::size_t>(line.side)], 0.0F, dy);
  if (look != nullptr) {
    mark.colour = look->name;
  }
  list.sprite(mark);
  if (look != nullptr && look->here && line.side < static_cast<int>(style.lit_side_marks.size())) {
    Sprite lit = shifted(style.lit_side_marks[static_cast<std::size_t>(line.side)], 0.0F, dy);
    lit.colour = look->name;
    list.sprite(lit);
  }
}

} // namespace phantomcrash
