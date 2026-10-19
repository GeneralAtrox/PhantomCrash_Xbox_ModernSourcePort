#include "phantomcrash/sv_purchase.hpp"
#include "phantomcrash/shop_pointer.hpp"
namespace phantomcrash {
InputFrame SvPurchase::pointer_input(InputFrame input) {
  if (view_ == PurchaseView::trade && input.clicked) {
    for (int i = 0; i < static_cast<int>(script_.actions.size()); ++i) {
      if (shop_hit(input, script_.actions[i].sprite)) {
        action_ = i;
        input.pressed |= script_.confirm;
        break;
      }
    }
  } else if (view_ == PurchaseView::hangars && scroll_ >= 1) {
    const auto &style = script_.hangars;
    if (shop_clicked(input, style.cancel_hint)) {
      input.pressed |= script_.cancel;
    }
    if (input.mouse_wheel > 0 || (input.clicked && shop_hit(input, style.arrows[0]))) {
      input.pressed |= script_.left;
    }
    if (input.mouse_wheel < 0 || (input.clicked && shop_hit(input, style.arrows[1]))) {
      input.pressed |= script_.right;
    }
    if (!input.clicked || !input.pointer) {
      return input;
    }
    const auto p = *input.pointer;
    if (p[0] < style.clip[0] || p[0] >= style.clip[2] || p[1] < style.clip[1] ||
        p[1] >= style.clip[3]) {
      return input;
    }
    const int reach = style.shown / 2 + 1;
    for (int k = -reach; k <= reach; ++k) {
      const int n = (hangar_ + k + garage_hangar_count) % garage_hangar_count;
      bool clicked = false;
      for (Sprite hit : style.numbers[n]) {
        hit.position[0] += style.centre[0] + k * style.step;
        hit.position[1] += style.centre[1];
        clicked = clicked || shop_hit(input, hit);
      }
      if (!clicked) {
        continue;
      }
      if (k == 0) {
        input.pressed |= script_.confirm;
      } else {
        previous_hangar_ = hangar_;
        hangar_ = n;
        scroll_ = 0;
        sounds_.push_back(script_.jump_sound);
      }
      break;
    }
  }
  return input;
}
} // namespace phantomcrash
