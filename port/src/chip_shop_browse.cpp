// Port additions to the BUY and SELL dialog: it opens on the CHIP's card,
// and left/right step to the neighbouring CHIPs without leaving it
// (ChipDialogBrowse); each is set against the CHIP fitted to the robot
// ridden (ChipCompareStyle), so the CHIPs can be compared at a glance.

#include "phantomcrash/chip_shop.hpp"

#include <algorithm>

namespace phantomcrash {

std::optional<ChipOffer> ChipShop::fitted() const {
  const Garage &garage = quest_.garage;
  if (garage.riding < 0) {
    return std::nullopt;
  }
  for (const auto &[index, item] : garage.stock) {
    if (item.kind != PartKind::chip || item.hangar != garage.riding) {
      continue;
    }
    const ChipRecord *chip = record(ChipOffer{item.id});
    if (chip == nullptr) {
      return std::nullopt;
    }
    return ChipOffer{item.id, item.level, item.name.empty() ? chip->name : item.name,
                     0,       index,      true};
  }
  return std::nullopt;
}

float ChipShop::card_slide() const {
  const double length = script_.cards.slide_seconds;
  return length > 0.0 ? static_cast<float>(std::clamp(card_seconds_ / length, 0.0, 1.0)) : 1.0F;
}

// On the card: left/right step the CHIP, down or confirm move to the trade,
// cancel goes back to the cards. On the options: up moves back to the card.
bool ChipShop::card_input(const double tick_seconds, const InputFrame &input) {
  if (!script_.dialog.browse) {
    return false;
  }
  if (!on_card_) {
    if (!input.any_pressed(script_.up)) {
      return false;
    }
    on_card_ = true;
    side_.reset();
    sound(script_.row_sound);
    return true;
  }
  bool fresh = false;
  if (const int step = side_.step(tick_seconds, input, script_.left, script_.right, fresh)) {
    step_card(step);
    return true;
  }
  if (input.any_pressed(script_.down | script_.confirm_buttons)) {
    on_card_ = false;
    choice_ = 0; // the trade
    side_.reset();
    sound(script_.row_sound);
    return true;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    sound(script_.select_sound);
    close_dialog(true);
  }
  return true;
}

// The cards behind stay put on the new CHIP, so leaving the dialog shows
// it in the middle without a slide.
void ChipShop::step_card(const int step) {
  const int count = static_cast<int>(offers_.size());
  if (count < 2) {
    return;
  }
  cursor_ = previous_ = (cursor_ + step + count) % count;
  slide_direction_ = 0;
  slide_seconds_ = script_.cards.slide_seconds;
  card_direction_ = step;
  card_seconds_ = 0.0;
  sound(script_.move_sound);
  menu_help();
}

} // namespace phantomcrash
