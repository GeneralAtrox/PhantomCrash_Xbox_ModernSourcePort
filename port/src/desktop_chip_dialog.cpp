#include "desktop_chip_dialog.hpp"
#include "phantomcrash/money.hpp"

#include "desktop_chip_compare.hpp"
#include "desktop_chip_spec_rows.hpp"
#include "desktop_draw_list.hpp"
#include "phantomcrash/town_screen.hpp"

#include <cmath>

namespace phantomcrash {
namespace {

Sprite centred_at(Sprite sprite, const std::array<float, 2> &at,
                  const std::array<float, 2> &scale = {1.0F, 1.0F}) {
  sprite.position = at;
  sprite.centered = true;
  sprite.size = std::array<float, 2>{(sprite.rect[2] - sprite.rect[0]) * scale[0],
                                     (sprite.rect[3] - sprite.rect[1]) * scale[1]};
  return sprite;
}

Sprite shifted(Sprite sprite, const float dx) {
  sprite.position[0] += dx;
  return sprite;
}

void number(DrawList &list, const NumberFont &font, const NumberStyle &style,
            const std::int64_t value, const int layer) {
  for (const Sprite &digit : number_sprites(font, style, value, layer)) {
    list.sprite(digit);
  }
}

void draw_reticle(DrawList &list, const ChipSpecPanel &panel, const std::array<Sprite, 4> &corners,
                  const Sprite &fill, const std::array<float, 2> &half) {
  const float dx = std::fabs(half[0]) * panel.preview_scale;
  const float dy = std::fabs(half[1]) * panel.preview_scale;
  const auto [cx, cy] = panel.centre;
  const std::array<std::array<float, 2>, 4> at{
      {{cx - dx, cy - dy}, {cx + dx, cy - dy}, {cx - dx, cy + dy}, {cx + dx, cy + dy}}};
  for (std::size_t corner = 0; corner < corners.size(); ++corner) {
    list.sprite(centred_at(corners[corner], at[corner]));
  }
  Sprite box = centred_at(fill, panel.centre);
  box.size = std::array<float, 2>{dx * 2.0F, dy * 2.0F};
  list.sprite(box);
}

void draw_reticles(DrawList &list, const ChipSpecPanel &panel, const ChipSpec &spec) {
  draw_reticle(list, panel, panel.inner_corners, panel.inner_fill, spec.inner_half);
  draw_reticle(list, panel, panel.outer_corners, panel.outer_fill, spec.outer_half);
}

// The panel grown out of the card centred at `card_at`, `grow` of the way.
void draw_panel(DrawList &list, const ChipDialog &dialog, const std::array<float, 2> &card_at,
                const float grow) {
  Sprite panel = centred_at(dialog.panel, dialog.panel_to);
  if (grow >= 1.0F) {
    panel.layer = dialog.panel_open_layer;
  }
  const float x = card_at[0] + (dialog.panel_to[0] - card_at[0]) * grow;
  if (!dialog.rows) {
    list.sprite(centred_at(panel, {x, dialog.panel_to[1]}, {grow, grow}));
    return;
  }
  const ChipSpecRows &rows = *dialog.rows;
  const auto centre = stretched_centre(panel, rows);
  draw_scaled(list, stretched_art(panel, rows.window_caps, std::nullopt, rows), centre,
              {x, centre[1]}, {grow, grow});
}

void draw_pops(DrawList &list, const ChipDialog &dialog, const float pop) {
  const std::array<float, 2> scale = pop_open(pop, 1.0F, dialog.pop_thin);
  for (std::size_t index = 0; index < dialog.pops.size(); ++index) {
    const Sprite frame = centred_at(dialog.pops[index], dialog.pops[index].position);
    if (!dialog.rows || (index != dialog.rows->preview_pop && index != dialog.rows->box_pop)) {
      list.sprite(centred_at(frame, frame.position, scale));
      continue;
    }
    const ChipSpecRows &rows = *dialog.rows;
    const bool box = index == rows.box_pop;
    const auto pieces = box ? stretched_art(frame, rows.box_caps, rows.box_plain, rows)
                            : stretched_art(frame, rows.preview_caps, std::nullopt, rows);
    const auto centre = stretched_centre(frame, rows);
    draw_scaled(list, pieces, centre, centre, scale);
  }
}

// The taller panel: the reticles in the middle of the stretched preview and
// the rows in the stretched stats box.
void draw_spec_rows(DrawList &list, const NumberFont &font, const ChipDialog &dialog,
                    const ChipRowsShown &shown) {
  const ChipSpecRows &rows = *dialog.rows;
  ChipSpecPanel panel = dialog.spec;
  panel.centre[1] += rows.grow * 0.5F - rows.raise;
  draw_reticles(list, panel, shown.spec);
  draw_fitted_chip(list, panel, rows, shown, dialog.spec.labels.layer);
  if (rows.box_pop < dialog.pops.size()) {
    const Sprite &box = dialog.pops[rows.box_pop];
    draw_chip_spec_rows(list, font, rows, centred_at(box, box.position), dialog.spec.labels.layer,
                        shown);
  }
}

// The card chosen in the dialog: the carousel's highlight behind it and,
// with more than one CHIP, the arrows either side.
void draw_browse(DrawList &list, const ChipShopScript &script, const bool arrows) {
  Sprite highlight = script.highlight;
  highlight.position[0] += script.dialog.card_shift;
  list.sprite(highlight);
  if (arrows) {
    for (const Sprite &arrow : script.dialog.browse->arrows) {
      list.sprite(arrow);
    }
  }
}

// Comparing, the fitted CHIP, unless it is the one shown (selling it).
void compare_with_fitted(ChipRowsShown &shown, const ChipShop &shop, const ChipOffer &offer) {
  const ChipDialog &dialog = shop.script().dialog;
  const std::optional<ChipOffer> fitted = shop.fitted();
  if (!shop.comparing() || !dialog.rows || !dialog.rows->compare || !fitted ||
      fitted->stock == offer.stock) {
    return;
  }
  if (const ChipRecord *chip = shop.record(*fitted)) {
    shown.fitted_chip = chip;
    shown.fitted_name = fitted->name;
    shown.fitted_level = fitted->level;
    shown.fitted_spec = chip_spec(dialog.spec, *chip, fitted->level);
  }
}

} // namespace

void draw_chip_spec(DrawList &list, const NumberFont &font, const ChipSpecPanel &panel,
                    const ChipSpec &spec) {
  list.sprite(panel.labels);
  list.sprite(panel.inner_shapes[static_cast<std::size_t>(spec.inner_shape)]);
  list.sprite(panel.outer_shapes[static_cast<std::size_t>(spec.outer_shape)]);
  const int layer = panel.labels.layer;
  number(list, font, panel.inner_size[0], shown_reticle_size(spec.inner_half[0]), layer);
  number(list, font, panel.inner_size[1], shown_reticle_size(spec.inner_half[1]), layer);
  number(list, font, panel.outer_size[0], shown_reticle_size(spec.outer_half[0]), layer);
  number(list, font, panel.outer_size[1], shown_reticle_size(spec.outer_half[1]), layer);
  draw_reticles(list, panel, spec);
  number(list, font, panel.mortal_shot, spec.mortal_shot, layer);
  number(list, font, panel.repulsion_shield, spec.repulsion_shield, layer);
}

void draw_chip_card(DrawList &list, const ChipCards &cards, const ChipRecord *chip,
                    const std::array<float, 2> at, const float scale, const std::uint32_t colour,
                    const float alpha, const bool clipped) {
  constexpr std::array<float, 4> whole{0.0F, 0.0F, 640.0F, 480.0F};
  const std::array<float, 4> &clip = clipped ? cards.clip : whole;
  Sprite card = centred_at(cards.card, at, {scale, scale});
  card.colour = colour;
  list.sprite(card, clip, alpha);
  if (chip != nullptr && chip->face) {
    Sprite face = centred_at(*chip->face, {at[0], at[1] + cards.face_drop * scale}, {scale, scale});
    face.layer = cards.face_layer;
    face.colour = colour;
    list.sprite(face, clip, alpha);
  }
}

std::vector<Sprite> chip_card_detail_sprites(const NumberFont &font, const ChipDetails &details,
                                             const ChipOffer &offer, const ChipSpec &spec) {
  std::vector<Sprite> sprites{details.level_label, details.weight_label, details.weight_unit};
  for (const auto &digit :
       number_sprites(font, details.level, offer.level, details.level_label.layer)) {
    sprites.push_back(digit);
  }
  for (const auto &digit :
       number_sprites(font, details.weight, spec.weight, details.weight_label.layer)) {
    sprites.push_back(digit);
  }
  return sprites;
}

void draw_chip_card_details(DrawList &list, const NumberFont &font, const ChipDetails &details,
                            const ChipOffer &offer, const ChipSpec &spec, const float shift,
                            const float alpha) {
  for (const Sprite &sprite : chip_card_detail_sprites(font, details, offer, spec)) {
    list.sprite(shifted(sprite, shift), alpha);
  }
}

// 0x030bdb: UPLIFT's window, popped open with the frames; once open, the
// bar as long as the level chosen, both levels and the price.
void draw_level_window(DrawList &list, const HudStyle &hud, const ChipShop &shop,
                       const ChipLevelWindow &level, const float pop, const int layer) {
  list.sprite(centred_at(level.window, level.window.position, pop_open(pop, 1.0F, 0.05F)));
  if (pop < 1.0F) {
    return;
  }
  Sprite bar = level.bar;
  bar.position = {level.bar_at[0], level.bar_at[1] - level.bar_height * 0.5F};
  bar.centered = false;
  bar.size =
      std::array<float, 2>{level.bar_step * static_cast<float>(shop.level()), level.bar_height};
  list.sprite(bar);
  const ChipOffer &offer = shop.offers()[static_cast<std::size_t>(shop.cursor())];
  number(list, hud.font, level.old_level, offer.level, layer);
  number(list, hud.font, level.new_level, shop.level(), layer);
  NumberStyle price = level.price;
  if (shop.uplift_price() > shop.money()) {
    price.colour = level.poor_colour;
  }
  for (const Sprite &sprite : money_sprites(hud, price, shown_ny(shop.uplift_price()), layer)) {
    list.sprite(sprite);
  }
  if (shop.view() == ChipShopView::dialog) {
    for (const Sprite &hint : level.confirm_hint) {
      list.sprite(hint);
    }
    for (const Sprite &hint : level.cancel_hint) {
      list.sprite(hint);
    }
  }
}

void draw_chip_dialog(DrawList &list, const HudStyle &hud, const ChipShop &shop) {
  const NumberFont &font = hud.font;
  if (!shop.dialog_open() || shop.offers().empty()) {
    return;
  }
  const ChipShopScript &script = shop.script();
  const ChipDialog &dialog = script.dialog;
  const ChipOffer &offer = shop.offers()[static_cast<std::size_t>(shop.cursor())];
  const ChipRecord *chip = shop.record(offer);
  // Uplifting, the fire control shows at the level chosen.
  const int level = dialog.level ? shop.level() : offer.level;
  const ChipSpec spec = chip != nullptr ? chip_spec(dialog.spec, *chip, level) : ChipSpec{};
  const ChipSpec card_spec =
      chip != nullptr ? chip_spec(dialog.spec, *chip, offer.level) : ChipSpec{};
  // Stage one: the card slides aside with its LEVEL and weight, out past
  // the carousel's clip, so it is drawn whole.
  const float shift = shop.dialog_stage(0) * dialog.card_shift;
  const std::array<float, 2> card_at{script.cards.centre[0] + shift, script.cards.centre[1]};
  // Browsing, a new CHIP's card slides in from its side and fades in.
  const float slide = shop.card_slide();
  const float nudge =
      dialog.browse ? (1.0F - slide) * dialog.browse->slide * shop.card_direction() : 0.0F;
  if (shop.on_card() && shop.dialog_stage(2) >= 1.0F) {
    draw_browse(list, script, shop.offers().size() > 1);
  }
  draw_chip_card(list, script.cards, chip, {card_at[0] + nudge, card_at[1]}, 1.0F, 0xffffff, slide,
                 false);
  draw_chip_card_details(list, font, script.details, offer, card_spec, shift + nudge, slide);
  // Stage two: the panel grows out of the card to its place.
  if (const float grow = shop.dialog_stage(1); grow > 0.0F) {
    draw_panel(list, dialog, card_at, grow);
  }
  // Stage three: the frames pop open; then the rest shows.
  const float pop = shop.dialog_stage(2);
  if (pop <= 0.0F) {
    return;
  }
  draw_pops(list, dialog, pop);
  if (dialog.level) {
    draw_level_window(list, hud, shop, *dialog.level, pop, dialog.spec.labels.layer);
  }
  if (pop < 1.0F) {
    return;
  }
  if (dialog.rows) {
    ChipRowsShown shown{chip, spec, level, std::nullopt, offer.level};
    if (dialog.level) {
      shown.owned_spec = card_spec;
    }
    compare_with_fitted(shown, shop, offer);
    draw_spec_rows(list, font, dialog, shown);
  } else {
    draw_chip_spec(list, font, dialog.spec, spec);
  }
  for (std::size_t option = 0; option < dialog.options.size(); ++option) {
    list.sprite(dialog.options[option].sprite);
    if (static_cast<int>(option) == shop.choice() && shop.view() == ChipShopView::dialog &&
        !shop.on_card()) {
      list.sprite(dialog.options[option].lit);
    }
  }
}

} // namespace phantomcrash
