#include "phantomcrash/used_sv.hpp"
#include "phantomcrash/shop_pointer.hpp"
#include <algorithm>

namespace phantomcrash {
InputFrame UsedSv::pointer_input(InputFrame input) {
  if (!input.clicked || !opened() || done() || !faded_in() || purchase_.open()) {
    return input;
  }
  for (int i = 0; i < item_count(); ++i) {
    const int slot = slot_of(i);
    Sprite hit = icon(i).sprite;
    hit.position = {slot_x(slot), script_.slots[slot][1]};
    if (!shop_hit(input, hit)) {
      continue;
    }
    cursor_ = i;
    menu_help();
    input.pressed |= script_.confirm_buttons;
    break;
  }
  return input;
}
} // namespace phantomcrash
