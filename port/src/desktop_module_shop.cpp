#include "desktop_module_shop.hpp"
#include "phantomcrash/money.hpp"

#include "desktop_draw_list.hpp"
#include "desktop_extra_stats.hpp"
#include "desktop_part_stats.hpp"
#include "desktop_shop_list.hpp"
#include "phantomcrash/canvas.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace phantomcrash {
ModuleShopScreen::ModuleShopScreen(TextureCache &textures, const GlyphFont &font, ModuleShop logic,
                                   const MessageBoxStyle &box, const std::string &from,
                                   const FrameStyle &frame, const std::vector<MusicCredit> &credits,
                                   QuestSave quest, PreviewWorld world,
                                   const RobotPreviewScript &preview)
    : TownScreen(textures, font, logic.script(), from, frame, credits, std::move(quest)),
      number_font_(frame.hud.font), logic_(std::move(logic)),
      notice_(box, logic_.script().message_text_at),
      preview_(world, preview,
               logic_.script().mode == ModuleShopMode::stock ? "stock_module" : "module_shop") {}

void ModuleShopScreen::tick(const InputFrame &raw_input, const double tick_seconds) {
  const auto &script = logic_.script();
  const InputFrame input =
      logic_.view() == ModuleShopView::list && logic_.list_seconds() >= script.list.window.seconds
          ? sort_pointer(raw_input, script.sort_name, script.sort_price)
          : raw_input;
  const auto before_view = logic_.view();
  logic_.update(tick_seconds, notice_.pointer_input(input, logic_.script().confirm_buttons));
  if (before_view != logic_.view()) {
    SDL_Log("Module shop: mode=%d category=%d view=%d->%d mouse=%d", int(logic_.script().mode),
            logic_.category(), int(before_view), int(logic_.view()), input.clicked);
  }
  tick_frame(tick_seconds, logic_.faded_in());
  notice_.update(logic_.message(), logic_.view() == ModuleShopView::question, logic_.choice(),
                 logic_.script().messages, tick_seconds);
  const QuestSave &now = logic_.quest();
  if (quest().money != now.money || quest().garage != now.garage) {
    set_quest(now);
  }
  // 0x084680: a body, legs, arm or shoulder alone, by its part id (BUY and
  // SELL MODULE through 0x0225b0).
  const ShopGoods *chosen = logic_.chosen();
  const auto kind = chosen == nullptr ? std::nullopt : stock_kind(chosen->goods.kind);
  if (kind && *kind != PartKind::chip && *kind != PartKind::module) {
    preview_.show(preview_part(*kind, chosen->goods.id));
  } else {
    preview_.hide();
  }
  preview_.set_lift(details_fit(chosen).raise);
  preview_.tick(tick_seconds);
}

std::vector<std::string> ModuleShopScreen::take_sounds() {
  std::vector<std::string> sounds = logic_.take_sounds();
  for (std::string &name : notice_.take_sounds()) {
    sounds.push_back(std::move(name));
  }
  return sounds;
}

// 0x1f61e: the three gears turning and the plate sliding in.
void ModuleShopScreen::draw_backdrop(DrawList &list) const {
  const ModuleShopScript &script = logic_.script();
  for (Sprite gear : script.gears) {
    gear.angle = logic_.gear_angle();
    list.sprite(gear);
  }
  if (script.plate.image.empty()) {
    return;
  }
  Sprite plate = script.plate;
  plate.position[0] = logic_.plate_x();
  list.sprite(plate);
}

// 0x1f61e with 0x06c530: the icons slide in; the chosen one is lit over
// its plain one, pulsing while the grid takes input.
void ModuleShopScreen::draw_grid(DrawList &list) const {
  const ModuleShopScript &script = logic_.script();
  const float slide = logic_.slide();
  for (std::size_t cell = 0; cell < script.categories.size(); ++cell) {
    const ShopCategory &category = script.categories[cell];
    if (!category.shown) {
      continue;
    }
    const float dx = (category.from_x - category.icon.position[0]) * (1.0F - slide);
    list.sprite(shifted(category.icon, dx, 0.0F));
    if (static_cast<int>(cell) == logic_.category()) {
      Sprite lit = shifted(category.lit, dx, 0.0F);
      if (logic_.view() != ModuleShopView::categories) {
        lit.animation = Animation::none;
      }
      list.sprite(lit);
    }
  }
}

// 0x1f1f5: the panel's pictures fading into each other and the strip
// rolling up through its window.
void ModuleShopScreen::draw_side(DrawList &list) const {
  const ShopSidePanel &side = logic_.script().side;
  if (side.pictures.empty()) {
    return;
  }
  for (const Sprite &panel : side.panels) {
    list.sprite(panel);
  }
  const float fade = logic_.picture_fade();
  const auto picture = [&side](const int index) {
    return side.pictures[static_cast<std::size_t>(index) % side.pictures.size()];
  };
  if (fade < 1.0F) {
    list.sprite(picture(logic_.previous_picture()), 1.0F - fade);
  }
  list.sprite(picture(logic_.picture()), fade);
  const float offset = logic_.strip_offset();
  list.sprite(shifted(side.strip, 0.0F, -offset), side.strip_clip);
  list.sprite(shifted(side.strip, 0.0F, side.strip_period - offset), side.strip_clip);
}

// 0x1f8a3 (buy) and 0x21cd2 (sell): the list's goods, a row each, with
// their maker's icon (selling: the hangar a part is fitted in, or the mark
// of a paint or emblem worn), and the chosen one's details.
void ModuleShopScreen::draw_rows(DrawList &list) const {
  draw_goods_rows(list, font_, logic_.script().list, logic_.goods_list());
  if (const ShopGoods *line = logic_.chosen()) {
    draw_chosen(list, *line);
  }
}

// The chosen goods' labels, stats and price; a paint's or emblem's picture.
void ModuleShopScreen::draw_chosen(DrawList &list, const ShopGoods &line) const {
  const ModuleShopScript &script = logic_.script();
  const ShopGoodsList &style = script.list;
  const Goods &goods = line.goods;
  const bool module = goods.kind == GoodsKind::module;
  const DetailsFit fit = details_fit(&line);
  Sprite names =
      module ? (reduces_lasers(logic_.garage(), goods) ? style.laser_names : style.module_names)
             : script.categories[static_cast<std::size_t>(logic_.category())].stat_names;
  names.position[1] -= fit.raise;
  if (fit.squeeze != 1.0F) {
    names.size =
        std::array{names.rect[2] - names.rect[0], (names.rect[3] - names.rect[1]) * fit.squeeze};
  }
  PartStatsStyle stats = style.stats;
  stats.step = fit.step;
  if (module && style.module_stats_right > 0.0F) {
    stats.right = style.module_stats_right;
  }
  const float values_top =
      (module ? style.module_stats_top : style.stats_top) - fit.raise - fit.lift;
  if (const auto page = second_page(&line); !page.empty()) {
    // The second page letters its own lines where the art's would be.
    draw_extra_stats(list, font_, *script.extra_stats, page, names, 0, stats, values_top);
  } else {
    list.sprite(names);
    const auto lines = part_stats(logic_.table(), logic_.parts(), logic_.garage(), goods);
    draw_part_stats(list, font_, stats, lines, goods, values_top);
    if (script.extra_stats) {
      draw_extra_stats(list, font_, *script.extra_stats, chosen_extras(&line), names, lines.size(),
                       stats, values_top);
    }
  }
  // STOCK MODULE shows no price.
  if (script.mode != ModuleShopMode::stock) {
    NumberStyle price = style.price;
    if (module) {
      price.right[1] += style.module_rise;
    }
    price.right[1] -= fit.raise;
    for (const Sprite &digit :
         money_sprites(hud(), price, shown_ny(line.price), style.stats.layer)) {
      list.sprite(digit);
    }
  }
  // 0x0bb78a and 0x0bb7e8: the picture by the entry's index.
  const auto &entries =
      goods.kind == GoodsKind::paint ? logic_.garage().paints : logic_.garage().emblems;
  const auto &swatches =
      goods.kind == GoodsKind::paint ? script.paint_swatches : script.emblem_swatches;
  if ((goods.kind == GoodsKind::paint || goods.kind == GoodsKind::emblem) && goods.id >= 0 &&
      goods.id < static_cast<int>(entries.size())) {
    const auto index = static_cast<std::size_t>(entries[static_cast<std::size_t>(goods.id)].index);
    if (index < swatches.size()) {
      Sprite swatch = swatches[index];
      swatch.position = style.swatch_at;
      swatch.layer = style.swatch_layer;
      list.sprite(swatch);
    }
  }
}

// A part's details with extra lines close up and move up where the style
// says (extra_stats part_raise and part_step); otherwise the windows grow a
// line for each.
ModuleShopScreen::DetailsFit ModuleShopScreen::details_fit(const ShopGoods *line) const {
  const ShopGoodsList &style = logic_.script().list;
  DetailsFit fit{0.0F, 0.0F, style.stats.step, 1.0F, 0.0F};
  if (line == nullptr) {
    return fit;
  }
  const auto printed = static_cast<float>(
      part_stats(logic_.table(), logic_.parts(), logic_.garage(), line->goods).size());
  // The lines shown: the art's and its extras, or the second page's.
  const auto page = static_cast<float>(second_page(line).size());
  const float shown = page > 0.0F ? page : printed + static_cast<float>(chosen_extras(line).size());
  const float extras = shown - printed;
  fit.grow = style.stats.step * std::max(extras, 0.0F);
  const auto &extra = logic_.script().extra_stats;
  if (extras <= 0.0F || line->goods.kind == GoodsKind::module || !extra ||
      extra->part_step <= 0.0F) {
    return fit;
  }
  fit.step = extra->part_step;
  if (extra->part_lines > 0 && shown > static_cast<float>(extra->part_lines)) {
    fit.step *= static_cast<float>(extra->part_lines) / shown;
  }
  fit.squeeze = fit.step / style.stats.step;
  fit.grow = std::max(0.0F, shown * fit.step - printed * style.stats.step);
  fit.raise = extra->part_raise;
  // The squeezed art's separators come up from under each line's value.
  fit.lift = extra->line_rows[0] * (1.0F - fit.squeeze);
  return fit;
}

std::vector<ExtraStatLine> ModuleShopScreen::second_page(const ShopGoods *line) const {
  if (line == nullptr || !logic_.details_page()) {
    return {};
  }
  return detail_stats(logic_.table(), logic_.parts(), logic_.garage(), line->goods);
}

// The chosen goods' extra stat lines; none without the style for them.
std::vector<ExtraStatLine> ModuleShopScreen::chosen_extras(const ShopGoods *line) const {
  if (line == nullptr || !logic_.script().extra_stats) {
    return {};
  }
  return extra_stats(logic_.table(), logic_.parts(), logic_.garage(), line->goods);
}

// 0x1f8a3 and 0x21cd2: the grid dimmed, the list window and the details
// windows popping open, then what they hold.
void ModuleShopScreen::draw_list(DrawList &list) const {
  const ModuleShopScript &script = logic_.script();
  const ShopGoodsList &style = script.list;
  const double seconds = logic_.list_seconds();
  list.sprite(style.shade, style.shade_alpha);
  const bool open = draw_pop_window(list, style.window, seconds);
  if (open) {
    list.sprite(script.categories[static_cast<std::size_t>(logic_.category())].title);
    list.sprite(
        shifted(style.cursor, 0.0F, style.row_step * static_cast<float>(logic_.list_row())));
    if (logic_.view() == ModuleShopView::list) {
      for (const Sprite &hint : style.cancel_hint) {
        list.sprite(hint);
      }
    }
  }
  if (logic_.view() == ModuleShopView::list) {
    draw_sort_hints(list, logic_.goods_list());
  }
  const ShopGoods *line = logic_.chosen();
  const bool module = line != nullptr && line->goods.kind == GoodsKind::module;
  const auto &details = module ? style.module_details : style.details;
  const DetailsFit fit = details_fit(line);
  const auto placed = [&fit](const PopWindow &window) {
    PopWindow out = grown_window(window, fit.grow);
    out.sprite.position[1] -= fit.raise;
    return out;
  };
  draw_pop_window(list, placed(details[1]), seconds);
  if (draw_pop_window(list, placed(details[0]), seconds)) {
    for (const Sprite &label : style.price_label) {
      list.sprite(shifted(label, 0.0F, (module ? style.module_rise : 0.0F) - fit.raise));
    }
    if (line != nullptr &&
        (line->goods.kind == GoodsKind::paint || line->goods.kind == GoodsKind::emblem)) {
      list.sprite(style.swatch_frame);
    }
    if (logic_.has_details()) {
      for (const Sprite &arrow : script.extra_stats->page_arrows) {
        list.sprite(shifted(arrow, 0.0F, -fit.raise));
      }
    }
  }
  if (open) {
    draw_rows(list);
  }
}

void ModuleShopScreen::draw(const Viewport &viewport) {
  draw_town(viewport, logic_.seconds(), logic_.black_alpha(), [this, &viewport](DrawList &list) {
    const ModuleShopScript &script = logic_.script();
    draw_backdrop(list);
    preview_.draw(list, viewport, textures_.renderer());
    draw_grid(list);
    if (logic_.list_open()) {
      draw_list(list);
    } else {
      draw_side(list);
    }
    notice_.draw(list);
    list.ticker(script.help_style, glyph_run(font_, logic_.help_text()), logic_.help_seconds());
  });
}

} // namespace phantomcrash
