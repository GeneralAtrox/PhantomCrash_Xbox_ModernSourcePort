#include "phantomcrash/chip_shop.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace phantomcrash {
namespace {

float progress(const double seconds, const double length) {
  return static_cast<float>(std::clamp(seconds / length, 0.0, 1.0));
}

const ChipRecord *chip_record(const PartTables &parts, const int chip) {
  if (chip < 0 || chip >= static_cast<int>(parts.chips.size())) {
    return nullptr;
  }
  return &parts.chips[static_cast<std::size_t>(chip)];
}

int rounded(const float value) {
  return static_cast<int>(std::lround(value));
}

} // namespace

std::vector<ChipOffer> chips_for_sale(const PartTables &parts, const GarageTables &garage,
                                      const int lineup) {
  std::vector<ChipOffer> offers;
  if (lineup < 0 || lineup >= static_cast<int>(garage.chip_lineups.size())) {
    return offers;
  }
  for (const RobotPart &part : garage.chip_lineups[static_cast<std::size_t>(lineup)]) {
    if (const ChipRecord *chip = chip_record(parts, part.id)) {
      offers.push_back({part.id, part.level, chip->name,
                        part_price(parts, garage, PartKind::chip, part.id, part.level)});
    }
  }
  return offers;
}

std::vector<ChipOffer> chips_in_stock(const PartTables &parts, const GarageTables &garage,
                                      const Garage &owned) {
  std::vector<ChipOffer> offers;
  for (const auto &[index, item] : owned.stock) {
    if (item.kind != PartKind::chip || chip_record(parts, item.id) == nullptr) {
      continue;
    }
    offers.push_back({item.id, item.level, item.name,
                      part_price(parts, garage, PartKind::chip, item.id, item.level), index,
                      item.hangar >= 0});
  }
  return offers;
}

// 0x234dc: the reticles' sizes as a battle sizes them (0x04db69), the
// shown width and height being twice the half size.
ChipSpec chip_spec(const ChipSpecPanel &panel, const ChipRecord &chip, const int level) {
  ChipSpec spec;
  spec.inner_shape = std::clamp(chip.inner_shape, 0, 2);
  spec.outer_shape = std::clamp(chip.outer_shape, 0, 2);
  spec.inner_half = reticle_half_size(panel.inner_sizes[static_cast<std::size_t>(spec.inner_shape)],
                                      chip.inner_value.value(level), panel.value_max);
  spec.outer_half = reticle_half_size(panel.outer_sizes[static_cast<std::size_t>(spec.outer_shape)],
                                      chip.outer_value.value(level), panel.value_max);
  spec.mortal_shot = rounded(chip.critical_percent.value(level));
  spec.repulsion_shield = rounded(chip.dodge_percent.value(level));
  spec.weight = rounded(chip.weight.value(level));
  return spec;
}

ChipShop::ChipShop(ChipShopScript script, std::vector<ChipOffer> offers, QuestSave quest,
                   const PartTables &parts, const int resume_item)
    : TownLogic(script), script_(std::move(script)), offers_(std::move(offers)),
      quest_(std::move(quest)), parts_(parts),
      side_(script_.repeat_delay, script_.repeat_interval) {
  const int count = static_cast<int>(offers_.size());
  cursor_ = previous_ = count > 0 ? std::clamp(resume_item, 0, count - 1) : 0;
  slide_seconds_ = script_.cards.slide_seconds;
  on_exit_ = offers_.empty();
  if (offers_.empty() && script_.mode != ChipShopMode::buy) {
    show(ChipShopView::notice, "none", After::leave);
  }
  menu_help();
}

const ChipRecord *ChipShop::record(const ChipOffer &offer) const {
  return chip_record(parts_, offer.chip);
}

float ChipShop::slide() const {
  return progress(slide_seconds_, script_.cards.slide_seconds);
}

float ChipShop::window_open() const {
  return progress(seconds(), script_.window_seconds);
}

float ChipShop::card_alpha(const int ring) const {
  const double fade = script_.cards.fade_seconds;
  return progress(seconds() - script_.window_seconds - fade * ring, fade);
}

bool ChipShop::settled() const {
  return card_alpha(script_.cards.side) >= 1.0F && slide() >= 1.0F;
}

float ChipShop::scan_offset() const {
  const double travelled = seconds() * script_.scan_speed;
  return script_.scan_from + static_cast<float>(std::fmod(travelled, script_.scan_period));
}

float ChipShop::dialog_stage(const int stage) const {
  if (!in_dialog_) {
    return 0.0F;
  }
  double start = 0.0;
  for (int before = 0; before < stage; ++before) {
    start += script_.dialog.stage_seconds[static_cast<std::size_t>(before)];
  }
  return progress(view_seconds_ - start,
                  script_.dialog.stage_seconds[static_cast<std::size_t>(stage)]);
}

void ChipShop::set_help(const std::string &text) {
  if (text != help_) {
    help_ = text;
    help_seconds_ = 0.0;
  }
}

void ChipShop::menu_help() {
  if (on_exit_ || offers_.empty()) {
    set_help(script_.exit_help);
    return;
  }
  const auto chip = static_cast<std::size_t>(offers_[static_cast<std::size_t>(cursor_)].chip);
  set_help(chip < script_.chip_help.size() ? script_.chip_help[chip] : std::string{});
}

void ChipShop::show(const ChipShopView view, const std::string &message, const After after) {
  view_ = view;
  message_ = message;
  after_ = after;
  if (view == ChipShopView::question) {
    choice_ = 1; // NO
  }
}

void ChipShop::close_dialog(const bool pulse) {
  view_ = ChipShopView::browse;
  in_dialog_ = false;
  on_card_ = false;
  message_.clear();
  highlight_pulse_ = pulse;
  side_.reset();
  menu_help();
}

void ChipShop::update(const double tick_seconds, const InputFrame &raw_input) {
  const InputFrame input = pointer_input(raw_input);
  if (done()) {
    return;
  }
  help_seconds_ += tick_seconds;
  view_seconds_ += tick_seconds;
  slide_seconds_ += tick_seconds;
  card_seconds_ += tick_seconds;
  if (!advance(tick_seconds)) {
    return;
  }
  if (view_ == ChipShopView::notice || view_ == ChipShopView::question) {
    message_input(input);
  } else if (view_ == ChipShopView::dialog) {
    dialog_input(tick_seconds, input);
  } else if (settled()) {
    browse_input(tick_seconds, input);
  }
}

// 0x26de3 (buy) and 0x2700b (sell): left/right slide the cards, up/down
// move between them and EXIT, confirm opens the CHIP's dialog or leaves,
// cancel jumps to EXIT.
void ChipShop::browse_input(const double tick_seconds, const InputFrame &input) {
  const int count = static_cast<int>(offers_.size());
  bool fresh = false;
  const int step = side_.step(tick_seconds, input, script_.left, script_.right, fresh);
  if (step != 0 && !on_exit_ && count > 1) {
    previous_ = cursor_;
    cursor_ = (cursor_ + step + count) % count;
    slide_direction_ = step;
    slide_seconds_ = 0.0;
    sound(script_.move_sound);
    menu_help();
    return;
  }
  if (input.any_pressed(script_.up | script_.down) && count > 0) {
    on_exit_ = !on_exit_;
    sound(script_.row_sound);
    menu_help();
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    sound(script_.select_sound);
    if (on_exit_) {
      leave(script_.exit_to);
    } else if (script_.mode == ChipShopMode::uplift) {
      open_uplift();
    } else {
      open_dialog();
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons) && !on_exit_) {
    on_exit_ = true;
    sound(script_.jump_sound);
    menu_help();
  }
}

void ChipShop::open_dialog() {
  view_ = ChipShopView::dialog;
  in_dialog_ = true;
  view_seconds_ = 0.0;
  choice_ = 1; // CANCEL (0x26924)
  on_card_ = script_.dialog.browse.has_value();
  card_seconds_ = script_.cards.slide_seconds;
  highlight_pulse_ = false;
  message_.clear();
}

// 0x26331 (buy) and 0x26500 (sell): once grown, left/right choose the
// trade or CANCEL; cancel moves to CANCEL.
void ChipShop::dialog_input(const double tick_seconds, const InputFrame &input) {
  if (dialog_stage(2) < 1.0F) {
    return;
  }
  if (input.any_pressed(button::y)) {
    comparing_ = !comparing_;
    sound(script_.choice_sound);
    return;
  }
  if (script_.mode == ChipShopMode::uplift) {
    level_input(tick_seconds, input);
    return;
  }
  if (card_input(tick_seconds, input)) {
    return;
  }
  bool fresh = false;
  const int step = side_.step(tick_seconds, input, script_.left, script_.right, fresh);
  if (const int moved = std::clamp(choice_ + step, 0, 1); moved != choice_) {
    choice_ = moved;
    sound(script_.choice_sound);
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    if (choice_ == 0) {
      trade();
    } else {
      sound(script_.select_sound);
      close_dialog(true);
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons) && choice_ != 1) {
    choice_ = 1;
    sound(script_.jump_sound);
  }
}

// Buying (0x26331): the stock must have room and the money must do; the
// CHIP then joins the stock under its record's name, to be named. Selling
// (0x26500): a fitted CHIP cannot be sold; otherwise it asks first.
void ChipShop::trade() {
  const ChipOffer &offer = offers_[static_cast<std::size_t>(cursor_)];
  if (script_.mode == ChipShopMode::sell) {
    sound(script_.select_sound);
    if (offer.equipped) {
      show(ChipShopView::notice, "equipped", After::browse);
    } else {
      show(ChipShopView::question, "sell", After::sold);
    }
    return;
  }
  if (quest_.garage.free_stock() <= 0) {
    show(ChipShopView::notice, "no_stock", After::browse);
    return;
  }
  if (offer.price > quest_.money) {
    show(ChipShopView::notice, "poor", After::browse);
    return;
  }
  naming_stock_ =
      quest_.garage.add_stock({PartKind::chip, offer.chip, offer.level, offer.name, -1});
  quest_.money -= offer.price;
  sound(script_.trade_sound);
  show(ChipShopView::notice, "thanks", After::name_chip);
}

// 0x025460: the CHIP leaves the stock for its share of the price.
void ChipShop::sell_chosen() {
  const ChipOffer offer = offers_[static_cast<std::size_t>(cursor_)];
  quest_.money += static_cast<std::int64_t>(static_cast<double>(offer.price) * script_.sell_rate);
  quest_.garage.stock.erase(offer.stock);
  offers_.erase(offers_.begin() + cursor_);
  sound(script_.trade_sound);
  const int count = static_cast<int>(offers_.size());
  cursor_ = previous_ = std::min(cursor_, std::max(count - 1, 0));
  slide_direction_ = 0;
  if (offers_.empty()) {
    message_.clear();
    leave(script_.exit_to);
  } else {
    close_dialog(true);
  }
}

// 0x2530d (buy) and 0x25460, 0x2566c (sell): the notices, and the sell
// question with YES and NO.
void ChipShop::message_input(const InputFrame &input) {
  if (input.dialog_choice) {
    choice_ = *input.dialog_choice;
  }
  if (view_ == ChipShopView::question) {
    if (input.any_pressed(script_.left | script_.right)) {
      choice_ = 1 - choice_;
      sound(script_.choice_sound);
      return;
    }
    if (input.any_pressed(script_.cancel_buttons) && choice_ != 1) {
      choice_ = 1;
      sound(script_.jump_sound);
      return;
    }
  }
  if (!input.any_pressed(script_.confirm_buttons)) {
    return;
  }
  sound(script_.select_sound);
  if (view_ == ChipShopView::question) {
    answer(choice_ == 0);
    return;
  }
  switch (after_) {
  case After::browse:
    close_dialog(true);
    break;
  case After::dialog:
    view_ = ChipShopView::dialog;
    message_.clear();
    break;
  case After::name_chip:
    show(ChipShopView::notice, "name_chip", After::name_entry);
    break;
  case After::name_entry:
    message_.clear();
    leave("name_entry");
    break;
  case After::leave:
    message_.clear();
    leave(script_.exit_to);
    break;
  case After::sold:
    sell_chosen();
    break;
  case After::see_level:
  case After::uplifted:
    close_dialog(true);
    break;
  }
}

// A question answered: YES sells, uplifts or (a CHIP at the top level)
// shows its dialog anyway; NO goes back to the dialog or the cards.
void ChipShop::answer(const bool yes) {
  switch (after_) {
  case After::see_level:
    if (yes) {
      open_dialog();
      set_help(script_.dialog_help);
    } else {
      close_dialog(true);
    }
    return;
  case After::uplifted:
    if (yes) {
      uplift_chosen();
      return;
    }
    break;
  default:
    if (yes) {
      show(ChipShopView::notice, "sold", After::sold);
      return;
    }
    break;
  }
  view_ = ChipShopView::dialog;
  message_.clear();
}

} // namespace phantomcrash
