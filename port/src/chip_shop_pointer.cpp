#include "phantomcrash/chip_shop.hpp"
#include "phantomcrash/shop_pointer.hpp"
#include <algorithm>

namespace phantomcrash {
InputFrame ChipShop::pointer_input(InputFrame input) {
  if (done() || !faded_in()) {
    return input;
  }
  if (view_ == ChipShopView::dialog && dialog_stage(2) >= 1) {
    if (script_.dialog.level) {
      if (shop_clicked(input, script_.dialog.level->confirm_hint)) {
        input.pressed |= script_.confirm_buttons;
      }
      if (shop_clicked(input, script_.dialog.level->cancel_hint)) {
        input.pressed |= script_.cancel_buttons;
      }
    } else {
      for (int i = 0; input.clicked && i < static_cast<int>(script_.dialog.options.size()); ++i) {
        if (shop_hit(input, script_.dialog.options[i].sprite)) {
          on_card_ = false;
          choice_ = i;
          input.pressed |= script_.confirm_buttons;
          return input;
        }
      }
      if (const auto &browse = script_.dialog.browse) {
        // The wheel or an arrow steps the CHIP; clicking the card chooses it.
        if (input.mouse_wheel != 0) {
          on_card_ = true;
          input.pressed |= input.mouse_wheel > 0 ? script_.left : script_.right;
        }
        for (int i = 0; input.clicked && i < 2; ++i) {
          if (shop_hit(input, browse->arrows[static_cast<std::size_t>(i)])) {
            on_card_ = true;
            input.pressed |= i == 0 ? script_.left : script_.right;
            return input;
          }
        }
        Sprite card = script_.cards.card;
        card.centered = true;
        card.position = {script_.cards.centre[0] + script_.dialog.card_shift,
                         script_.cards.centre[1]};
        if (input.clicked && shop_hit(input, card)) {
          on_card_ = true;
        }
      }
    }
  } else if (view_ == ChipShopView::browse && settled()) {
    if (input.clicked && shop_hit(input, script_.exit.sprite)) {
      on_exit_ = true;
      input.pressed |= script_.confirm_buttons;
      return input;
    }
    if (offers_.empty()) {
      return input;
    }
    if (input.mouse_wheel != 0) {
      on_exit_ = false;
      input.pressed |= input.mouse_wheel > 0 ? script_.left : script_.right;
    }
    for (int i = 0; input.clicked && i < static_cast<int>(script_.row_arrows.size()); ++i) {
      if (!on_exit_ && shop_hit(input, script_.row_arrows[i])) {
        input.pressed |= i == 0 ? script_.left : script_.right;
        return input;
      }
    }
    const auto &cards = script_.cards;
    const int count = static_cast<int>(offers_.size());
    if (!input.clicked || !input.pointer) {
      return input;
    }
    const auto p = *input.pointer;
    if (p[0] < cards.clip[0] || p[0] >= cards.clip[2] || p[1] < cards.clip[1] ||
        p[1] >= cards.clip[3]) {
      return input;
    }
    for (int k = cards.side; k >= -cards.side; --k) {
      if (std::abs(k) >= count) {
        continue;
      }
      const float scale = k == 0 ? 1 : cards.side_scale;
      Sprite hit = cards.card;
      hit.centered = true;
      hit.position = {cards.centre[0] + k * cards.step,
                      cards.centre[1] + (k == 0 ? 0 : cards.side_drop)};
      hit.size =
          std::array{(hit.rect[2] - hit.rect[0]) * scale, (hit.rect[3] - hit.rect[1]) * scale};
      if (!shop_hit(input, hit) || card_alpha(std::abs(k)) <= 0) {
        continue;
      }
      on_exit_ = false;
      cursor_ = previous_ = (cursor_ + k + count) % count;
      slide_direction_ = 0;
      menu_help();
      input.pressed |= script_.confirm_buttons;
      return input;
    }
  }
  return input;
}
} // namespace phantomcrash
