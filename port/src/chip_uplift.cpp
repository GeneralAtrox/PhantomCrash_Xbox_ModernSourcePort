// PlusTech's UPLIFT: the level window of the CHIP chosen (0x032865), the
// question and the CHIP raised in the stock (0x032ca8).

#include "phantomcrash/chip_shop.hpp"

#include <algorithm>

namespace phantomcrash {

std::int64_t ChipShop::uplift_price() const {
  if (offers_.empty()) {
    return 0;
  }
  const ChipOffer &offer = offers_[static_cast<std::size_t>(cursor_)];
  const ChipRecord *chip = record(offer);
  if (chip == nullptr) {
    return 0;
  }
  // 0x02f3d4: the price at the new level less the price at the old.
  const auto price = [chip](const int level) {
    return static_cast<std::int64_t>(chip->price.value(level));
  };
  return std::max<std::int64_t>(0, price(level_) - price(offer.level));
}

// 0x032a8c: a CHIP at the top level cannot go higher, but its dialog can
// still be seen (YES chosen); any other opens the level window at its level.
void ChipShop::open_uplift() {
  const ChipOffer &offer = offers_[static_cast<std::size_t>(cursor_)];
  level_ = offer.level;
  level_control_.reset();
  if (offer.level >= script_.level_max) {
    show(ChipShopView::question, "maxed", After::see_level);
    choice_ = 0; // YES
    return;
  }
  open_dialog();
  set_help(script_.dialog_help);
}

// 0x032865: left/right choose a level from the CHIP's own up to the top;
// confirm on a new one asks (or says the money will not do); cancel closes
// the window.
void ChipShop::level_input(const double tick_seconds, const InputFrame &input) {
  const ChipOffer &offer = offers_[static_cast<std::size_t>(cursor_)];
  const ChipLevelWindow &bar = *script_.dialog.level;
  const LevelMeterHit hit{bar.bar_at[0], bar.bar_step, bar.bar_at[1] - bar.bar_height * 0.5F,
                          bar.bar_at[1] + bar.bar_height * 0.5F};
  const LevelChange change =
      level_control_.update(tick_seconds, input, level_, offer.level, script_.level_max,
                            script_.left, script_.right, script_.level_control, hit);
  if (change.value != level_) {
    level_ = change.value;
    if (change.sound) {
      sound(script_.choice_sound);
    }
    return;
  }
  if (input.any_pressed(script_.confirm_buttons) && level_ != offer.level) {
    sound(script_.select_sound);
    if (uplift_price() > quest_.money) {
      show(ChipShopView::notice, "poor", After::browse);
    } else {
      show(ChipShopView::question, "uplift", After::uplifted);
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    sound(script_.back_sound);
    close_dialog(true);
  }
}

// 0x032ca8: paid for (sys 27), the CHIP's level in the stock is raised.
void ChipShop::uplift_chosen() {
  ChipOffer &offer = offers_[static_cast<std::size_t>(cursor_)];
  quest_.money -= uplift_price();
  if (const auto item = quest_.garage.stock.find(offer.stock); item != quest_.garage.stock.end()) {
    item->second.level = level_;
  }
  if (const ChipRecord *chip = record(offer)) {
    offer.price = static_cast<std::int64_t>(chip->price.value(level_));
  }
  offer.level = level_;
  sound(script_.trade_sound);
  show(ChipShopView::notice, "thanks", After::browse);
}

} // namespace phantomcrash
