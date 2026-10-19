#include "desktop_chip_shop.hpp"
#include "phantomcrash/money.hpp"

#include "desktop_chip_dialog.hpp"
#include "desktop_draw_list.hpp"
#include "phantomcrash/canvas.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace phantomcrash {
namespace {

float lerp(const float from, const float to, const float t) {
  return from + (to - from) * t;
}

std::uint32_t grey(const int level) {
  const auto value = static_cast<std::uint32_t>(std::clamp(level, 0, 255));
  return value << 16 | value << 8 | value;
}

Sprite coloured(Sprite sprite, const std::uint32_t colour) {
  sprite.colour = colour;
  return sprite;
}

} // namespace

ChipShopScreen::ChipShopScreen(TextureCache &textures, const GlyphFont &font, ChipShop logic,
                               const MessageBoxStyle &box, const std::string &from,
                               const FrameStyle &frame, const std::vector<MusicCredit> &credits,
                               QuestSave quest, int &remembered)
    : TownScreen(textures, font, logic.script(), from, frame, credits, std::move(quest)),
      number_font_(frame.hud.font), logic_(std::move(logic)),
      notice_(box, logic_.script().message_text_at), remembered_(remembered) {}

void ChipShopScreen::tick(const InputFrame &input, const double tick_seconds) {
  const int before = logic_.cursor();
  logic_.update(tick_seconds, notice_.pointer_input(input, logic_.script().confirm_buttons));
  if (before != logic_.cursor()) {
    SDL_Log("CHIP carousel: count=%zu from=%d to=%d direction=%d", logic_.offers().size(), before,
            logic_.cursor(), logic_.slide_direction());
  }
  tick_frame(tick_seconds, logic_.faded_in());
  notice_.update(logic_.message(), logic_.view() == ChipShopView::question, logic_.choice(),
                 logic_.script().messages, tick_seconds);
  remembered_ = logic_.cursor();
  const QuestSave &now = logic_.quest();
  if (quest().money != now.money || quest().garage.free_stock() != now.garage.free_stock()) {
    set_quest(now);
  }
}

std::vector<std::string> ChipShopScreen::take_sounds() {
  std::vector<std::string> sounds = logic_.take_sounds();
  for (std::string &name : notice_.take_sounds()) {
    sounds.push_back(std::move(name));
  }
  return sounds;
}

// 0x20690: the window pops open, dimmed on EXIT and under the dialog; once
// open its arrows and the rolling scanlines show, and over the chosen card
// the highlight and the yellow arrows.
void ChipShopScreen::draw_window(DrawList &list) const {
  const ChipShopScript &script = logic_.script();
  const bool dim = logic_.on_exit() || logic_.dialog_open();
  const std::uint32_t colour = dim ? script.window_dim : 0xffffff;
  const float open = logic_.window_open();
  const auto scale = pop_open(open, script.window_widest, script.window_thin);
  Sprite window = coloured(script.window, colour);
  window.centered = true;
  window.size = std::array<float, 2>{(window.rect[2] - window.rect[0]) * scale[0],
                                     (window.rect[3] - window.rect[1]) * scale[1]};
  list.sprite(window);
  if (open < 1.0F) {
    return;
  }
  for (const Sprite &piece : script.frame) {
    list.sprite(coloured(piece, colour));
  }
  Sprite scan = coloured(script.scanlines, colour);
  scan.position[1] += logic_.scan_offset();
  list.sprite(scan, script.scan_clip);
  if (logic_.settled() && !dim && !logic_.offers().empty()) {
    Sprite highlight = script.highlight;
    if (logic_.highlight_pulses()) {
      highlight.animation = Animation::pulse;
    }
    list.sprite(highlight);
    for (const Sprite &arrow : script.row_arrows) {
      list.sprite(arrow);
    }
  }
}

// 0x20690: the chosen card in the middle, the rest smaller, lower and
// darker, fading in ring by ring; a move slides them a step. With the dialog
// open only the chosen card shows, drawn by the dialog (0x02f710 draws none).
void ChipShopScreen::draw_cards(DrawList &list) const {
  const ChipShopScript &script = logic_.script();
  const ChipCards &cards = script.cards;
  const int count = static_cast<int>(logic_.offers().size());
  if (count == 0 || logic_.window_open() < 1.0F || logic_.dialog_open()) {
    return;
  }
  const float t = logic_.slide();
  const int direction = logic_.slide_direction();
  const float offset = static_cast<float>(direction) * cards.step * (1.0F - t);
  for (int k = -cards.side; k <= cards.side; ++k) {
    if (std::abs(k) >= count) {
      continue;
    }
    float scale = cards.side_scale;
    float drop = cards.side_drop;
    if (k == 0) {
      scale = lerp(cards.side_scale, 1.0F, t);
      drop = lerp(cards.side_drop, 0.0F, t);
    } else if (k == -direction) {
      scale = lerp(1.0F, cards.side_scale, t);
      drop = lerp(0.0F, cards.side_drop, t);
    }
    const int ring = std::abs(k);
    const std::uint32_t colour =
        logic_.on_exit() ? script.window_dim : grey(255 - cards.shade * ring);
    const int item = ((logic_.cursor() + k) % count + count) % count;
    const ChipOffer &offer = logic_.offers()[static_cast<std::size_t>(item)];
    const std::array<float, 2> at{cards.centre[0] + static_cast<float>(k) * cards.step + offset,
                                  cards.centre[1] + drop};
    draw_chip_card(list, cards, logic_.record(offer), at, scale, colour, logic_.card_alpha(ring));
    const ChipRecord *chip = logic_.record(offer);
    const ChipSpec spec = chip ? chip_spec(script.dialog.spec, *chip, offer.level) : ChipSpec{};
    for (Sprite sprite : chip_card_detail_sprites(number_font_, script.details, offer, spec)) {
      sprite.position = {at[0] + (sprite.position[0] - cards.centre[0]) * scale,
                         at[1] + (sprite.position[1] - cards.centre[1]) * scale};
      const auto size = sprite.size.value_or(
          std::array{sprite.rect[2] - sprite.rect[0], sprite.rect[3] - sprite.rect[1]});
      sprite.size = std::array{size[0] * scale, size[1] * scale};
      // The carousel tint is grey; preserve each label/number's own colour.
      const auto tint = colour & 0xff;
      const auto channel = [&](int shift) {
        return ((sprite.colour >> shift) & 0xff) * tint / 255;
      };
      sprite.colour = (channel(16) << 16) | (channel(8) << 8) | channel(0);
      list.sprite(sprite, cards.clip, logic_.card_alpha(ring));
    }
  }
}

// 0x20690: once the window is open and the cards stand still (+0x684 at 8),
// without waiting for the side cards to fade in, the chosen CHIP's name and
// price. LEVEL and weight travel with every card in draw_cards.
// Focusing EXIT dims the carousel but keeps the selected CHIP's details.
void ChipShopScreen::draw_details(DrawList &list) const {
  const ChipShopScript &script = logic_.script();
  const ChipDetails &details = script.details;
  if (logic_.window_open() < 1.0F || logic_.slide() < 1.0F || logic_.offers().empty()) {
    return;
  }
  const ChipOffer &offer = logic_.offers()[static_cast<std::size_t>(logic_.cursor())];
  list.text(details.name_layer,
            layout_run(font_, glyph_run(font_, offer.name), details.name_at[0], details.name_at[1]),
            details.name_colour);
  for (const Sprite &digit :
       money_sprites(hud(), details.price, shown_ny(offer.price), details.name_layer)) {
    list.sprite(digit);
  }
}

// 0x20690: "3 / 9", or dashes for the CHIP on EXIT.
void ChipShopScreen::draw_counter(DrawList &list) const {
  const ChipCounter &counter = logic_.script().counter;
  const int layer = counter.slash.layer;
  if (logic_.on_exit() || logic_.offers().empty()) {
    for (const Sprite &dash : counter.dashes) {
      list.sprite(dash);
    }
  } else {
    for (const Sprite &digit :
         number_sprites(number_font_, counter.index, logic_.cursor() + 1, layer)) {
      list.sprite(digit);
    }
  }
  list.sprite(counter.slash);
  const auto total = static_cast<std::int64_t>(logic_.offers().size());
  for (const Sprite &digit : number_sprites(number_font_, counter.total, total, layer)) {
    list.sprite(digit);
  }
}

void ChipShopScreen::draw(const Viewport &viewport) {
  draw_town(viewport, logic_.seconds(), logic_.black_alpha(), [this](DrawList &list) {
    const ChipShopScript &script = logic_.script();
    draw_window(list);
    draw_cards(list);
    draw_details(list);
    draw_counter(list);
    for (const Sprite &label : script.labels) {
      list.sprite(label);
    }
    // 0x20690: EXIT shows while no dialog is open.
    if (!logic_.dialog_open()) {
      list.sprite(script.exit.sprite);
      if (logic_.on_exit()) {
        list.sprite(script.exit.lit);
      }
    }
    draw_chip_dialog(list, hud(), logic_);
    notice_.draw(list);
    list.ticker(script.help_style, glyph_run(font_, logic_.help_text()), logic_.help_seconds());
  });
}

} // namespace phantomcrash
