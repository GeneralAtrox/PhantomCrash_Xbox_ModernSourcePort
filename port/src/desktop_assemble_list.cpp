// ASSEMBLE's list, a line's details and the whole robot's totals.

#include "desktop_assemble.hpp"

#include "desktop_draw_list.hpp"
#include "desktop_extra_stats.hpp"
#include "desktop_part_stats.hpp"
#include "desktop_shop_list.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {

GoodsRowLook AssembleScreen::row_look(const ShopGoods &line) const {
  const AssembleScript &script = logic_.script();
  const bool usable = logic_.can_fit(line);
  const bool here = logic_.fitted_here(line);
  GoodsRowLook look;
  look.here = here;
  look.name =
      !usable ? script.unusable_colour : (here ? script.fitted_colour : script.list.name_colour);
  look.badge = usable && here ? script.fitted_colour : script.badge_colour;
  return look;
}

std::vector<GoodsRowLook> AssembleScreen::row_looks() const {
  std::vector<GoodsRowLook> looks;
  for (const ShopGoods &line : logic_.goods_list().goods()) {
    looks.push_back(row_look(line));
  }
  return looks;
}

// 0x018d50: the window pops open with the list's title (a side's for ARM
// and BACK), the cursor, the rows and the cancel hint; the details
// (0x019d24) show the chosen line alone in the short window.
void AssembleScreen::draw_list(DrawList &list) const {
  const AssembleScript &script = logic_.script();
  const ShopGoodsList &style = script.list;
  const AssembleCell &cell = logic_.cell();
  const Sprite &title =
      cell.titles[std::min(static_cast<std::size_t>(logic_.side()), cell.titles.size() - 1)];
  const ShopGoods *line = logic_.chosen();
  const bool details = logic_.view() == AssembleView::details && line != nullptr;
  if (details) {
    if (draw_pop_window(list, style.details[0], logic_.details_seconds())) {
      list.sprite(title);
      list.sprite(style.cursor);
      if (style.row_plate) {
        list.sprite(*style.row_plate);
      }
      const GoodsRowLook look = row_look(*line);
      draw_goods_row(list, font_, style, *line, &look, 0.0F);
      if (look.here) {
        list.sprite(script.details.equipped);
      }
    }
    draw_details(list, *line);
    return;
  }
  if (logic_.view() == AssembleView::list) {
    draw_sort_hints(list, logic_.goods_list());
  }
  if (!draw_pop_window(list, style.window, logic_.list_seconds())) {
    return;
  }
  list.sprite(title);
  list.sprite(
      shifted(style.cursor, 0.0F, style.row_step * static_cast<float>(logic_.goods_list().row())));
  if (logic_.view() == AssembleView::list) {
    for (const Sprite &hint : style.cancel_hint) {
      list.sprite(hint);
    }
  }
  draw_goods_rows(list, font_, style, logic_.goods_list(), row_looks());
}

// 0x019d24: the box fades in with the labels; once it is in, the part's
// own stats (0x03cbc3) and the whole robot's (0x019489).
void AssembleScreen::draw_details(DrawList &list, const ShopGoods &line) const {
  const AssembleScript &script = logic_.script();
  const AssembleDetails &details = script.details;
  const AssembleCell &cell = logic_.cell();
  const float t =
      static_cast<float>(std::clamp(logic_.details_seconds() / details.fade_seconds, 0.0, 1.0));
  list.sprite(details.box, details.box_alpha * t);
  const bool lasers =
      line.goods.kind == GoodsKind::module && reduces_lasers(logic_.garage(), line.goods);
  const std::size_t labels = lasers && cell.labels.size() > 1 ? 1 : 0;
  const Sprite &art = cell.labels[labels];
  const std::vector<ExtraStatLine> page =
      logic_.details_page() ? logic_.details_lines() : std::vector<ExtraStatLine>{};
  if (page.empty()) {
    list.sprite(art, t);
  } else {
    // The second page keeps only the art's title.
    Sprite title = art;
    title.rect[3] = title.rect[1] + details.extra_title_rows;
    list.sprite(title, t);
  }
  if (logic_.has_details()) {
    for (const Sprite &arrow : details.extra_stats->page_arrows) {
      list.sprite(arrow, t);
    }
  }
  if (t < 1.0F) {
    return;
  }
  PartStatsStyle stats = script.list.stats;
  stats.right = cell.stats_right;
  stats.pair_gap = details.totals.pair_gap;
  if (!page.empty()) {
    draw_second_page(list, page, art, stats);
    draw_picture(list, line.goods);
    return;
  }
  const auto lines = part_stats(logic_.table(), logic_.parts(), logic_.garage(), line.goods);
  draw_part_stats(list, font_, stats, lines, line.goods, script.list.stats_top);
  draw_picture(list, line.goods);
  draw_totals(list, line, t);
}

// The second page's lines from the first line's place in the labels' art
// down over the totals', closing up when there are more than fit.
void AssembleScreen::draw_second_page(DrawList &list, const std::vector<ExtraStatLine> &lines,
                                      const Sprite &art, PartStatsStyle stats) const {
  const AssembleScript &script = logic_.script();
  const AssembleDetails &details = script.details;
  Sprite names = art;
  names.rect[1] += details.extra_rows_top;
  names.position[1] += details.extra_rows_top;
  const float room = names.rect[3] - names.rect[1];
  const float step = std::min(stats.step, room / static_cast<float>(lines.size()));
  if (step < stats.step) {
    names.size = std::array{names.rect[2] - names.rect[0], room * step / stats.step};
  }
  stats.step = step;
  draw_extra_stats(list, font_, *details.extra_stats, lines, names, 0, stats,
                   script.list.stats_top);
}

// 0x0179ed: the value as it would be, right-aligned, with an arrow up or
// down when the part changes it; 0x019018 shows a value below 0 as a minus
// sign alone.
void AssembleScreen::draw_value(DrawList &list, const double before, const double after,
                                const int decimals, const float right, const float y,
                                const float alpha) const {
  const AssembleTotalsStyle &style = logic_.script().details.totals;
  if (after < 0.0 && style.negative) {
    Sprite minus = *style.negative;
    minus.position = {right + style.negative_dx, y + style.negative_dy};
    list.sprite(minus, alpha);
    return;
  }
  const double scale = std::pow(10.0, decimals);
  const double shown_before = std::round(before * scale);
  const double shown_after = std::round(after * scale);
  const bool changed = shown_after != shown_before;
  const bool up = shown_after > shown_before;
  const std::optional<std::uint32_t> &marked = up ? style.up_colour : style.down_colour;
  const std::uint32_t colour = changed && marked ? *marked : style.colour;
  const std::vector<int> run = glyph_run(font_, stat_text({StatRow::level, after, decimals}));
  list.text(style.layer, layout_run(font_, run, right - run_width(font_, run), y), colour,
            std::nullopt, alpha);
  if (!changed) {
    return;
  }
  Sprite arrow = up ? style.up : style.down;
  arrow.position = {right + style.arrow_dx, y + style.arrow_dy};
  if (marked) {
    arrow.colour = *marked;
  }
  list.sprite(arrow, alpha);
}

// 0x019489: the list's lines of the whole robot, before and after; O.C's
// active time beside its charge time; a module cutting lasers shows the
// laser reduction instead.
void AssembleScreen::draw_totals(DrawList &list, const ShopGoods &line, const float alpha) const {
  const AssembleCell &cell = logic_.cell();
  const AssembleTotalsStyle &style = logic_.script().details.totals;
  const RobotTotals before = logic_.totals();
  const RobotTotals after = logic_.totals_with(line);
  const bool lasers =
      line.goods.kind == GoodsKind::module && reduces_lasers(logic_.garage(), line.goods);
  float y = cell.totals_top;
  for (TotalRow row : cell.totals) {
    if (row == TotalRow::overclock && lasers) {
      row = TotalRow::laser_reduction;
    }
    const int decimals = total_decimals(logic_.table(), row);
    switch (row) {
    case TotalRow::chip_name:
      list.text(style.layer,
                layout_run(font_, glyph_run(font_, after.chip_name), style.chip_name_at[0],
                           style.chip_name_at[1]),
                style.colour, std::nullopt, alpha);
      continue;
    case TotalRow::level:
      draw_value(list, after.chip_level, after.chip_level, decimals, cell.totals_right, y, alpha);
      break;
    case TotalRow::overclock:
      // The charge time stops at 0 (an Accelerator alone cuts it below).
      draw_value(list, std::max(0.0, before.charge_seconds), std::max(0.0, after.charge_seconds),
                 decimals, cell.totals_right, y, alpha);
      draw_value(list, before.active_seconds, after.active_seconds, decimals,
                 cell.totals_right - style.pair_gap, y, alpha);
      break;
    default:
      draw_value(list, total_value(before, row), total_value(after, row), decimals,
                 cell.totals_right, y, alpha);
      break;
    }
    y += style.step;
  }
}

} // namespace phantomcrash
