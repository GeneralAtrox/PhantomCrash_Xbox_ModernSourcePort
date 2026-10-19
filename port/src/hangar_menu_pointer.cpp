#include "phantomcrash/hangar_menu.hpp"
#include "phantomcrash/canvas.hpp"
#include "phantomcrash/shop_pointer.hpp"

namespace phantomcrash {

InputFrame HangarMenu::pointer_input(InputFrame input) {
  // A modal notice owns the pointer until it closes. ShopNotice maps its
  // visible buttons to confirm/dialog_choice before this input arrives.
  if (view_ == HangarView::buy || view_ == HangarView::notice) {
    return input;
  }
  if (view_ == HangarView::menu) {
    if (!input.clicked || menu_seconds_ < script_.menu.window.seconds) {
      return input;
    }
    for (std::size_t index = 0; index < script_.menu.items.size(); ++index) {
      const auto &item = script_.menu.items[index];
      if (can_choose(item.action) && shop_hit(input, item.sprite)) {
        item_ = static_cast<int>(index);
        refresh_help();
        input.pressed |= script_.confirm_buttons;
        break;
      }
    }
    return input;
  }
  if (input.mouse_wheel != 0) {
    input.pressed |= input.mouse_wheel > 0 ? script_.left : script_.right;
    return input;
  }
  if (!input.clicked || !input.pointer) {
    return input;
  }
  const auto select = [&](const int card, const bool activate) {
    if (card != card_) {
      choose_card(card);
      sound(script_.move_sound);
    }
    if (activate) {
      input.pressed |= script_.confirm_buttons;
    }
  };
  const HangarPlates &plates = script_.plates;
  const float slide = plates_slide();
  if (!plates.plates.empty()) {
    const float offset = (plates.from_x.back() - plates.to_x.back()) * (1 - slide);
    if (shop_hit(input, plates.exit, offset) ||
        (card_ == exit_card && shop_hit(input, plates.exit_lit))) {
      select(exit_card, true);
      return input;
    }
  }
  for (int index = static_cast<int>(plates.plates.size()) - 1; index >= 0; --index) {
    for (Sprite plate : plates.plates[index]) {
      plate.position[0] +=
          plates.from_x[index] + (plates.to_x[index] - plates.from_x[index]) * slide;
      plate.position[1] += plates.y;
      if (shop_hit(input, plate)) {
        select(index, false);
        return input;
      }
    }
  }
  // Cards are clipped to the authored canvas and submitted in ascending order.
  // Pick the last visible card at the pointer when their rectangles overlap.
  const auto [x, y] = *input.pointer;
  if (x < 0 || x >= canvas_width || y < 0 || y >= canvas_height) {
    return input;
  }
  const HangarCards &cards = script_.cards;
  for (int card = card_count - 1; card >= 0; --card) {
    if (shop_hit(input, card == exit_card ? cards.exit : cards.card, card_x(card), cards.y)) {
      select(card, true);
      break;
    }
  }
  return input;
}

} // namespace phantomcrash
