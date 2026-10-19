#include "desktop_dealer.hpp"
#include "phantomcrash/money.hpp"

#include "desktop_draw_list.hpp"
#include "desktop_shop_dialog.hpp"
#include "phantomcrash/canvas.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace phantomcrash {
namespace {

Sprite placed(Sprite sprite, const float x, const float y) {
  sprite.position = {x, y};
  return sprite;
}

float width_of(const Sprite &sprite) {
  return sprite.size ? (*sprite.size)[0] : sprite.rect[2] - sprite.rect[0];
}

float lerp(const float from, const float to, const float t) {
  return from + (to - from) * t;
}

} // namespace

DealerScreen::DealerScreen(TextureCache &textures, const GlyphFont &font, Dealer logic,
                           const MessageBoxStyle &box, const std::string &from,
                           const FrameStyle &frame, const std::vector<MusicCredit> &credits,
                           QuestSave quest, int &remembered, PreviewWorld world,
                           const RobotPreviewScript &preview)
    : TownScreen(textures, font, logic.script(), from, frame, credits, std::move(quest)),
      number_font_(frame.hud.font), logic_(std::move(logic)),
      purchase_(box, logic_.script().purchase), remembered_(remembered),
      preview_(world, preview, "dealer") {
  for (const Sprite &part : logic_.look().panorama) {
    static_cast<void>(textures_.get(part.image));
  }
  static_cast<void>(textures_.get(logic_.script().band.image));
}

void DealerScreen::tick(const InputFrame &input, const double tick_seconds) {
  const auto before = logic_.purchase().view();
  logic_.update(tick_seconds, purchase_.pointer_input(input, logic_.script().confirm_buttons));
  if (before != logic_.purchase().view()) {
    SDL_Log("Shop purchase: view=%d->%d action=%d mouse=%d", int(before),
            int(logic_.purchase().view()), logic_.purchase().action(), input.clicked);
  }
  tick_frame(tick_seconds, logic_.faded_in());
  purchase_.update(logic_.purchase(), tick_seconds);
  remembered_ = logic_.cursor();
  const QuestSave &now = logic_.quest();
  if (quest().money != now.money || quest().riding != now.riding) {
    set_quest(now);
  }
  // 0x039cc9: the SV under the cursor stands in the preview.
  const int offer = logic_.chosen_offer();
  if (offer >= 0 && logic_.menu_open() >= 1.0F && !logic_.closing()) {
    preview_.show(preview_record(logic_.offers()[static_cast<std::size_t>(offer)].robot));
  } else {
    preview_.hide();
  }
  preview_.tick(tick_seconds);
}

std::vector<std::string> DealerScreen::take_sounds() {
  std::vector<std::string> sounds = logic_.take_sounds();
  for (std::string &name : purchase_.take_sounds()) {
    sounds.push_back(std::move(name));
  }
  return sounds;
}

// 0x39585: the panorama scrolling left at half brightness and wrapping,
// the top and bottom, the line and the band sliding in; welcome as the
// menu opens, farewell as it closes.
void DealerScreen::draw_backdrop(DrawList &list, const Viewport &viewport) const {
  const DealerScript &script = logic_.script();
  const auto &panorama = logic_.look().panorama;
  float total = 0.0F;
  for (const Sprite &part : panorama) {
    total += width_of(part);
  }
  const auto offset =
      static_cast<float>(std::fmod(logic_.seconds() * script.scroll_speed, double{total}));
  const float left = viewport.visible_left(), right = viewport.visible_right();
  // A periodic strip across the actual viewport, including negative canvas x.
  // Textures stay cached; only the sampling position advances with time.
  const float first = -offset + std::floor((left + offset) / total) * total;
  for (float start = first; start < right; start += total) {
    float x = start;
    for (Sprite part : panorama) {
      const float width = width_of(part);
      if (x + width > left && x < right) {
        part.position = {x, part.position[1]};
        part.colour = script.panorama_colour;
        list.sprite(part);
      }
      x += width;
    }
  }
  list.sprite(logic_.look().top);
  list.sprite(logic_.look().bottom);
  list.sprite(script.line);
  const float band = logic_.band_open();
  Sprite slid = script.band;
  slid.position[0] += script.band_from * (1.0F - band);
  list.sprite(slid);
  const float greeting = (1.0F - logic_.menu_open()) * band;
  if (greeting > 0.0F) {
    list.sprite(logic_.closing() ? script.farewell : script.welcome, greeting);
  }
}

// 0x39e46: the logo between large and small; once the menu is open with
// its trail, each copy fainter than the one after it.
void DealerScreen::draw_logo(DrawList &list) const {
  const DealerLogo &style = logic_.script().logo;
  const auto logo_at = [&](const float t) {
    Sprite logo = logic_.look().logo;
    const float scale = lerp(1.0F, style.large_scale, t);
    logo.position = {lerp(style.small_at[0], style.large_at[0], t),
                     lerp(style.small_at[1], style.large_at[1], t)};
    logo.size = std::array<float, 2>{(logo.rect[2] - logo.rect[0]) * scale,
                                     (logo.rect[3] - logo.rect[1]) * scale};
    return logo;
  };
  const auto &trail = logic_.logo_trail();
  if (logic_.band_open() < 1.0F) {
    list.sprite(logo_at(trail.front()), style.opening_alpha * logic_.band_open());
    return;
  }
  if (logic_.menu_open() < 1.0F) {
    list.sprite(logo_at(trail.front()));
    return;
  }
  const int count = std::min(style.trail, static_cast<int>(trail.size()));
  for (int i = count - 1; i >= 0; --i) {
    const float alpha = std::pow(style.trail_fade, static_cast<float>(i));
    list.sprite(logo_at(trail[static_cast<std::size_t>(i)]), alpha);
  }
}

// 0x396be: the profile, the three SVs (their legs' icons) and EXIT
// sliding in from the left, the chosen one lit.
void DealerScreen::draw_menu(DrawList &list) const {
  const DealerScript &script = logic_.script();
  for (std::size_t i = 0; i < script.items.size(); ++i) {
    const auto index = static_cast<int>(i);
    const std::array<float, 2> home = script.items[i].sprite.position;
    const float x = std::min(script.item_from[i] + logic_.menu_open() * script.item_slide, home[0]);
    const ShopIcon &icon = logic_.item(index);
    if (logic_.offer_of(index) < 0 && script.items[i].id == "sv") {
      continue; // fewer than three SVs today
    }
    list.sprite(placed(icon.sprite, x, home[1]));
    if (index == logic_.cursor()) {
      list.sprite(placed(icon.lit, x, home[1]));
    }
  }
}

// 0x39db6: over an SV, the maker's badge, its name plate cross-fading from
// the last one and its price.
void DealerScreen::draw_offer(DrawList &list) const {
  const int offer = logic_.chosen_offer();
  if (offer < 0 || logic_.menu_open() < 1.0F) {
    return;
  }
  const DealerScript &script = logic_.script();
  const auto &pictures = logic_.look().pictures;
  const auto picture_of = [&](const int shown) -> const Sprite * {
    if (shown < 0) {
      return nullptr;
    }
    const int picture = logic_.offers()[static_cast<std::size_t>(shown)].picture;
    return picture >= 0 && picture < static_cast<int>(pictures.size())
               ? &pictures[static_cast<std::size_t>(picture)]
               : nullptr;
  };
  const float fade = logic_.picture_fade();
  const int before = logic_.offer_of(logic_.previous_cursor());
  if (const Sprite *last = picture_of(before); last != nullptr && fade < 1.0F && before != offer) {
    list.sprite(placed(*last, script.picture_at[0], script.picture_at[1]), 1.0F - fade);
  }
  if (const Sprite *now = picture_of(offer); now != nullptr) {
    list.sprite(placed(*now, script.picture_at[0], script.picture_at[1]), fade);
  }
  list.sprite(logic_.look().badge);
  list.sprite(script.price_label);
  for (const Sprite &digit : money_sprites(
           hud(), script.price, shown_ny(logic_.offers()[static_cast<std::size_t>(offer)].price),
           script.price_label.layer)) {
    list.sprite(digit);
  }
}

// 0x3a5bc: the panel grows open, then the corporation profile shows.
void DealerScreen::draw_profile(DrawList &list) const {
  const DealerScript &script = logic_.script();
  const float growing = growing_after(script.profile_growth, logic_.view_seconds());
  list.sprite(grown_panel(logic_.look().profile_panel, growing, script.profile_growth.from_scale));
  if (growing <= 0.0F) {
    draw_text_block(list, font_, logic_.look().profile);
  }
}

void DealerScreen::draw(const Viewport &viewport) {
  draw_town(viewport, logic_.seconds(), logic_.black_alpha(), [this, &viewport](DrawList &list) {
    const DealerScript &script = logic_.script();
    draw_backdrop(list, viewport);
    preview_.draw(list, viewport, textures_.renderer());
    draw_logo(list);
    draw_menu(list);
    draw_offer(list);
    if (logic_.view() == DealerView::profile) {
      draw_profile(list);
    }
    purchase_.draw(list, logic_.purchase(), logic_.quest().garage);
    if (logic_.menu_open() >= 1.0F && !logic_.closing()) {
      const HelpLine line{logic_.help_text(), script.help_trail};
      list.ticker(help_style(script.help_style, line), glyph_run(font_, line.text),
                  logic_.help_seconds());
    }
  });
}

} // namespace phantomcrash
