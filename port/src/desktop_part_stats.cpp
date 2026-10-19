#include "desktop_part_stats.hpp"

#include "desktop_draw_list.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {

void draw_part_stats(DrawList &list, const GlyphFont &font, const PartStatsStyle &style,
                     const std::vector<StatLine> &lines, const Goods &goods, const float top,
                     const StatComparison *comparison) {
  float y = top;
  for (const StatLine &line : lines) {
    std::string text = stat_text(line);
    std::uint32_t colour = style.colour;
    float scale = 1.0F;
    if (comparison) {
      const auto old = std::ranges::find(comparison->before, line.row, &StatLine::row);
      if (old != comparison->before.end()) {
        // Compare displayed values so rounding cannot show a coloured +/-0.
        const double delta = std::stod(text) - std::stod(stat_text(*old));
        if (std::abs(delta) > 1e-9) {
          colour = delta > 0 ? comparison->increase : comparison->decrease;
          if (line.row != StatRow::level) {
            text += " (" + std::string(delta > 0 ? "+" : "") +
                    stat_text({line.row, delta, line.decimals}) + ")";
            scale = comparison->scale;
          }
        }
      }
    }
    const std::vector<int> run = glyph_run(font, text);
    const float width = run_width(font, run);
    if (comparison && width > 0 && line.row != StatRow::level) {
      scale = std::min(scale, comparison->width / width);
    }
    auto quads = layout_run(font, run, 0, 0);
    // O.C ACTIVE paired on CHARGE's line, which comes next.
    const bool paired = style.pair_gap > 0.0F && line.row == StatRow::active_time;
    const float right = paired ? style.right - style.pair_gap : style.right;
    const float left = right - run_width(font, run) * scale;
    for (auto &quad : quads) {
      quad.x = left + quad.x * scale;
      quad.y = y + quad.y * scale + font.cell * (1.0F - scale) * 0.5F;
      quad.width *= scale;
      quad.height *= scale;
    }
    list.text(style.layer, std::move(quads), colour);
    // A CHIP's level has no word beside it.
    if (line.row == StatRow::level && goods.kind != GoodsKind::chip) {
      const std::size_t sign = goods.level < 0 ? 0 : (goods.level == 0 ? 1 : 2);
      Sprite word = style.signs[sign];
      word.position = {style.right + style.sign_offset, y};
      list.sprite(word);
    }
    if (!paired) {
      y += style.step;
    }
  }
}

} // namespace phantomcrash
