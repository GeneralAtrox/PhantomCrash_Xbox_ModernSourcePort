#include "phantomcrash/dealer.hpp"
#include "phantomcrash/shop_pointer.hpp"
#include <algorithm>

namespace phantomcrash {
InputFrame Dealer::pointer_input(InputFrame input) {
  if (!input.clicked || menu_open() < 1 || closing_ || done() || !faded_in() || purchase_.open()) {
    return input;
  }
  if (view_ == DealerView::profile) {
    if (view_seconds_ >= script_.profile_growth.seconds && shop_hit(input, look().profile_panel)) {
      input.pressed |= script_.cancel_buttons;
    }
    return input;
  }
  for (int i = 0; i < static_cast<int>(script_.items.size()); ++i) {
    if (offer_of(i) < 0 && script_.items[i].id == "sv") {
      continue;
    }
    Sprite hit = item(i).sprite;
    hit.position = script_.items[i].sprite.position;
    if (!shop_hit(input, hit)) {
      continue;
    }
    previous_ = cursor_;
    cursor_ = i;
    picture_ = 0;
    set_help(script_.help[item(cursor_).id]);
    input.pressed |= script_.confirm_buttons;
    break;
  }
  return input;
}
} // namespace phantomcrash
