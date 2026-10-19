#include "desktop_sv_purchase.hpp"

#include "desktop_draw_list.hpp"
#include "phantomcrash/shop_pointer.hpp"

#include <cmath>
#include <utility>

namespace phantomcrash {
namespace {

MessageBoxStyle placed_box(MessageBoxStyle style, const std::array<float, 2> &text_at) {
  style.text_position = text_at;
  return style;
}

} // namespace

float purchase_pulse(const double seconds) {
  return 0.75F + 0.25F * static_cast<float>(std::cos(seconds * 6.283185307179586));
}

ShopNotice::ShopNotice(const MessageBoxStyle &box, const std::array<float, 2> &text_at)
    : box_(placed_box(box, text_at)) {}

void ShopNotice::update(const std::string &message, const MessageBox::Kind kind, const int choice,
                        const Messages &messages, const double tick_seconds) {
  if (message != shown_) {
    shown_ = message;
    const auto found = messages.find(shown_);
    if (found == messages.end()) {
      box_.close();
    } else {
      box_.open(kind, found->second, choice);
      if (!box_.style().open_sound.empty()) {
        sounds_.push_back(box_.style().open_sound);
      }
    }
  }
  box_.set_choice(choice);
  box_.update(tick_seconds);
}

InputFrame ShopNotice::pointer_input(InputFrame input, const Buttons confirm) const {
  if (!grown() || !input.clicked) {
    return input;
  }
  if (box_.kind() == MessageBox::Kind::question) {
    if (shop_hit(input, box_.style().yes.normal)) {
      input.dialog_choice = 0;
    } else if (shop_hit(input, box_.style().no.normal)) {
      input.dialog_choice = 1;
    }
    if (input.dialog_choice) {
      input.pressed |= confirm;
      SDL_Log("Shop dialog: clicked %s", *input.dialog_choice == 0 ? "YES" : "NO");
    }
  } else if (box_.kind() == MessageBox::Kind::notice && shop_hit(input, box_.style().ok.normal)) {
    input.pressed |= confirm;
    SDL_Log("Shop dialog: clicked OK");
  }
  return input;
}

void ShopNotice::draw(DrawList &list) const {
  list.message_box(box_);
}

std::vector<std::string> ShopNotice::take_sounds() {
  return std::exchange(sounds_, {});
}

PurchaseOverlay::PurchaseOverlay(const MessageBoxStyle &box, const SvPurchaseScript &script)
    : notice_(box, script.message_text_at) {}

void PurchaseOverlay::update(const SvPurchase &purchase, const double tick_seconds) {
  notice_.update(purchase.message(), purchase.view() == PurchaseView::question, purchase.choice(),
                 purchase.script().messages, tick_seconds);
}

std::vector<std::string> PurchaseOverlay::take_sounds() {
  return notice_.take_sounds();
}

// 0x3a92a, 0x20353: the window's labels are part of it; the chosen one is
// lit and pulsing.
void PurchaseOverlay::draw_trade(DrawList &list, const SvPurchase &purchase) const {
  const SvPurchaseScript &script = purchase.script();
  list.sprite(script.trade_window);
  const Sprite &lit = script.actions[static_cast<std::size_t>(purchase.action())].lit;
  list.sprite(lit, purchase_pulse(purchase.view_seconds()));
}

// 0x399d8, 0x203bd: the hangar numbers scrolling in their window, empty
// hangars lit, the one chosen drawn again over itself and pulsing; the
// arrow the carousel scrolls towards lights up.
void PurchaseOverlay::draw_hangars(DrawList &list, const SvPurchase &purchase,
                                   const Garage &garage) const {
  const HangarCarousel &style = purchase.script().hangars;
  const int count = static_cast<int>(style.numbers.size());
  int delta = purchase.hangar() - purchase.previous_hangar();
  if (delta > count / 2) {
    delta -= count;
  } else if (delta < -count / 2) {
    delta += count;
  }
  const float scroll = purchase.hangar_scroll();
  const float at =
      static_cast<float>(purchase.previous_hangar()) + static_cast<float>(delta) * scroll;
  const int nearest = static_cast<int>(std::lround(at));
  list.sprite(style.panel);
  for (const Sprite &hint : style.cancel_hint) {
    list.sprite(hint);
  }
  const bool scrolling = scroll < 1.0F && delta != 0;
  for (std::size_t side = 0; side < style.arrows.size(); ++side) {
    Sprite arrow = style.arrows[side];
    if (scrolling && (delta < 0) == (side == 0)) {
      arrow.colour = style.arrow_lit;
    }
    list.sprite(arrow);
  }
  const int reach = style.shown / 2 + 1;
  for (int k = -reach; k <= reach; ++k) {
    const int hangar = ((nearest + k) % count + count) % count;
    const auto &shown = garage.hangars[static_cast<std::size_t>(hangar)];
    const bool empty = shown.owned && shown.empty();
    const bool chosen = hangar == purchase.hangar() && !scrolling;
    for (Sprite number : style.numbers[static_cast<std::size_t>(hangar)]) {
      number.position[0] += style.centre[0] + (static_cast<float>(nearest + k) - at) * style.step;
      number.position[1] += style.centre[1];
      number.layer = style.number_layer;
      number.colour = empty ? style.empty_colour : style.used_colour;
      list.sprite(number, style.clip);
      if (chosen) {
        number.layer = style.chosen_layer;
        number.colour = empty ? style.chosen_empty : style.chosen_used;
        list.sprite(number, purchase_pulse(purchase.view_seconds()));
      }
    }
  }
}

void PurchaseOverlay::draw(DrawList &list, const SvPurchase &purchase, const Garage &garage) const {
  if (purchase.view() == PurchaseView::trade) {
    draw_trade(list, purchase);
  } else if (purchase.view() == PurchaseView::hangars) {
    draw_hangars(list, purchase, garage);
  }
  notice_.draw(list);
}

} // namespace phantomcrash
