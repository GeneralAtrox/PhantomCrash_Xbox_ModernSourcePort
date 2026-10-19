#include "desktop_tune_shop.hpp"
#include "phantomcrash/money.hpp"

#include "desktop_draw_list.hpp"
#include "desktop_part_stats.hpp"
#include "desktop_shop_list.hpp"

#include <cmath>
#include <cstdlib>
#include <utility>

namespace phantomcrash {
namespace {

// The word beside a level: LIGHT below 0, NORMAL at 0, HEAVY above.
std::size_t sign_of(const int level) {
  return level < 0 ? 0 : (level == 0 ? 1 : 2);
}

// A weight or load in hundredths, as the number's decimals write it.
std::int64_t tonnes(const float hundredths, const float unit, const int decimals) {
  return std::llround(static_cast<double>(hundredths) * unit * std::pow(10.0, decimals));
}

} // namespace

TuneShopScreen::TuneShopScreen(TextureCache &textures, const GlyphFont &font, TuneShop logic,
                               const MessageBoxStyle &box, const std::string &from,
                               const FrameStyle &frame, const std::vector<MusicCredit> &credits,
                               QuestSave quest, PreviewWorld world,
                               const RobotPreviewScript &preview)
    : TownScreen(textures, font, logic.script(), from, frame, credits, std::move(quest)),
      number_font_(frame.hud.font), logic_(std::move(logic)),
      notice_(box, logic_.script().message_text_at), preview_(world, preview, "tune_shop") {}

void TuneShopScreen::tick(const InputFrame &raw_input, const double tick_seconds) {
  const int before_level = logic_.level();
  const auto before_view = logic_.view();
  const auto &script = logic_.script();
  const InputFrame input =
      logic_.view() == TuneShopView::list && logic_.list_seconds() >= script.list.window.seconds
          ? sort_pointer(raw_input, script.sort_name, script.sort_price)
          : raw_input;
  logic_.update(tick_seconds, notice_.pointer_input(input, logic_.script().confirm_buttons));
  if (before_level != logic_.level() || before_view != logic_.view()) {
    SDL_Log("Tune shop: view=%d->%d item=%d level=%d->%d held=0x%X stick=%.2f click=%d drag=%d",
            int(before_view), int(logic_.view()), logic_.item(), before_level, logic_.level(),
            input.held, input.left_stick[0], input.clicked, input.mouse_left_held);
  }
  tick_frame(tick_seconds, logic_.faded_in());
  notice_.update(logic_.message(), logic_.view() == TuneShopView::question, logic_.choice(),
                 logic_.script().messages, tick_seconds);
  const QuestSave &now = logic_.quest();
  if (quest().money != now.money || quest().garage != now.garage) {
    set_quest(now);
  }
  // 0x0301af: the chosen body, legs, arm or shoulder alone (0x084680).
  const ShopGoods *chosen = logic_.chosen();
  const auto kind = chosen == nullptr ? std::nullopt : stock_kind(chosen->goods.kind);
  if (kind && *kind != PartKind::chip && *kind != PartKind::module) {
    preview_.show(preview_part(*kind, chosen->goods.id));
  } else {
    preview_.hide();
  }
  preview_.tick(tick_seconds);
}

std::vector<std::string> TuneShopScreen::take_sounds() {
  std::vector<std::string> sounds = logic_.take_sounds();
  for (std::string &name : notice_.take_sounds()) {
    sounds.push_back(std::move(name));
  }
  return sounds;
}

void TuneShopScreen::draw_number(DrawList &list, const NumberStyle &style, const std::int64_t value,
                                 const int layer) const {
  for (const Sprite &digit : number_sprites(number_font_, style, value, layer)) {
    list.sprite(digit);
  }
}

// 0x02efe6: each drawing scrolls down at its own speed, faint, a second
// copy a period above it; then the plate.
void TuneShopScreen::draw_backdrop(DrawList &list) const {
  const TuneShopScript &script = logic_.script();
  for (const TuneStrip &strip : script.strips) {
    const auto offset = static_cast<float>(std::fmod(logic_.seconds() * strip.speed, strip.period));
    list.sprite(shifted(strip.sprite, 0.0F, offset), strip.alpha);
    list.sprite(shifted(strip.sprite, 0.0F, offset - strip.period), strip.alpha);
  }
  list.sprite(script.plate);
}

// 0x0302f3 and 0x0324bd: the icons slide in from the left; the chosen one
// is lit over its plain one, pulsing while the row takes input.
void TuneShopScreen::draw_items(DrawList &list) const {
  const TuneShopScript &script = logic_.script();
  const float slide = logic_.slide();
  for (std::size_t index = 0; index < script.items.size(); ++index) {
    const TuneItem &item = script.items[index];
    const float dx = (item.from_x - item.icon.position[0]) * (1.0F - slide);
    list.sprite(shifted(item.icon, dx, 0.0F));
    if (static_cast<int>(index) == logic_.item()) {
      Sprite lit = shifted(item.lit, dx, 0.0F);
      if (logic_.view() != TuneShopView::items) {
        lit.animation = Animation::none;
      }
      list.sprite(lit);
    }
  }
}

// 0x0303d7's chosen row: the stats at the level chosen and, for a part
// fitted, its robot's weight and the load its legs carry.
void TuneShopScreen::draw_chosen(DrawList &list) const {
  const TuneShopScript &script = logic_.script();
  const Goods tuned = logic_.tuned();
  const auto lines = part_stats(logic_.table(), logic_.parts(), logic_.garage(), tuned);
  StatComparison comparison;
  if (const ShopGoods *selected = logic_.chosen()) {
    comparison.before =
        part_stats(logic_.table(), logic_.parts(), logic_.garage(), selected->goods);
  }
  comparison.increase = script.increase_colour;
  comparison.decrease = script.decrease_colour;
  comparison.scale = script.comparison_scale;
  comparison.width = script.comparison_width;
  draw_part_stats(list, font_, script.list.stats, lines, tuned, script.list.stats_top, &comparison);
  const auto load = logic_.fitted_load();
  if (!load) {
    return;
  }
  const TuneLoad &style = script.load;
  const float unit = logic_.table().hundredths;
  draw_number(list, style.weight, tonnes(load->weight, unit, style.weight.decimals), style.layer);
  draw_number(list, style.capacity, tonnes(load->capacity, unit, style.capacity.decimals),
              style.layer);
  for (const Sprite &label : style.labels) {
    list.sprite(label);
  }
}

// 0x0303d7: the icons dimmed, the list window and the details windows
// popping open, then what they hold.
void TuneShopScreen::draw_list(DrawList &list) const {
  const TuneShopScript &script = logic_.script();
  const ShopGoodsList &style = script.list;
  const double seconds = logic_.list_seconds();
  list.sprite(style.shade, style.shade_alpha);
  const bool open = draw_pop_window(list, style.window, seconds);
  if (logic_.view() == TuneShopView::list) {
    draw_sort_hints(list, logic_.goods_list());
  }
  const float dy = style.row_step * static_cast<float>(logic_.goods_list().row());
  const TuneItem &item = script.items[static_cast<std::size_t>(logic_.item())];
  if (open) {
    list.sprite(item.title);
    Sprite cursor = shifted(style.cursor, 0.0F, dy);
    if (logic_.view() != TuneShopView::list) {
      cursor.animation = Animation::none;
    }
    list.sprite(cursor);
    if (logic_.view() == TuneShopView::list) {
      for (const Sprite &hint : style.cancel_hint) {
        list.sprite(hint);
      }
    }
  }
  draw_pop_window(list, style.details[1], seconds);
  if (draw_pop_window(list, style.details[0], seconds)) {
    list.sprite(item.stat_names);
  }
  if (open) {
    draw_goods_rows(list, font_, style, logic_.goods_list());
    draw_chosen(list);
  }
}

// 0x02f3fc: the list's cursor dark, the meter popping open, then the old
// level and the new with their words, the marker and the price, red when
// the money will not do.
void TuneShopScreen::draw_meter(DrawList &list) const {
  const TuneShopScript &script = logic_.script();
  const TuneMeter &meter = script.meter;
  const ShopGoods *line = logic_.chosen();
  if (line == nullptr) {
    return;
  }
  const float dy = script.list.row_step * static_cast<float>(logic_.goods_list().row());
  list.sprite(shifted(meter.cursor, 0.0F, dy));
  if (!draw_pop_window(list, meter.window, logic_.meter_seconds())) {
    return;
  }
  if (logic_.view() == TuneShopView::meter) {
    for (const Sprite &hint : meter.confirm_hint) {
      list.sprite(hint);
    }
    for (const Sprite &hint : meter.cancel_hint) {
      list.sprite(hint);
    }
  }
  const int old = line->goods.level;
  const int level = logic_.level();
  for (const auto &[value, at] : {std::pair{old, meter.old_sign_at}, {level, meter.new_sign_at}}) {
    Sprite sign = meter.signs[sign_of(value)];
    sign.position = at;
    sign.layer = meter.layer;
    list.sprite(sign);
  }
  draw_number(list, meter.old_level, std::abs(old), meter.layer);
  draw_number(list, meter.new_level, std::abs(level), meter.layer);
  list.sprite(shifted(meter.marker, meter.marker_step * static_cast<float>(level), 0.0F));
  NumberStyle price = meter.price;
  const std::int64_t cost = logic_.price();
  if (cost > logic_.quest().money) {
    price.colour = meter.poor_colour;
  }
  for (const Sprite &sprite : money_sprites(hud(), price, shown_ny(cost), meter.layer)) {
    list.sprite(sprite);
  }
}

void TuneShopScreen::draw(const Viewport &viewport) {
  draw_town(viewport, logic_.seconds(), logic_.black_alpha(), [this, &viewport](DrawList &list) {
    const TuneShopScript &script = logic_.script();
    draw_backdrop(list);
    preview_.draw(list, viewport, textures_.renderer());
    draw_items(list);
    if (logic_.list_open()) {
      draw_list(list);
    }
    if (logic_.meter_open()) {
      draw_meter(list);
    }
    notice_.draw(list);
    list.ticker(script.help_style, glyph_run(font_, logic_.help_text()), logic_.help_seconds());
  });
}

} // namespace phantomcrash
