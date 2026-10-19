#include "desktop_shop_dialog.hpp"

#include "desktop_draw_list.hpp"

namespace phantomcrash {

void draw_shop_dialog(DrawList &list, const GlyphFont &font, const ShopDialog &dialog,
                      const std::string &saying, const std::int64_t price) {
  const auto found = dialog.says.find(saying);
  if (found == dialog.says.end()) {
    return;
  }
  list.sprite(dialog.window);
  for (const Sprite &decoration : dialog.decorations) {
    list.sprite(decoration);
  }
  list.sprite(found->second.face);
  float y = dialog.text_at[1];
  for (const std::string &line : found->second.lines) {
    list.text(dialog.text_layer,
              layout_run(font, glyph_run(font, shop_text(line, price)), dialog.text_at[0], y),
              dialog.text_colour);
    y += dialog.line_step;
  }
}

void draw_text_block(DrawList &list, const GlyphFont &font, const TextBlock &block,
                     const float alpha) {
  float y = block.at[1];
  for (const std::string &line : block.lines) {
    list.text(block.layer, layout_run(font, glyph_run(font, line), block.at[0], y), block.colour,
              std::nullopt, alpha);
    y += block.line_step;
  }
}

} // namespace phantomcrash
