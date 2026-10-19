#include "phantomcrash/dealer.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
namespace {

int index_of(const std::vector<ShopIcon> &icons, const std::string &id) {
  for (std::size_t i = 0; i < icons.size(); ++i) {
    if (icons[i].id == id) {
      return static_cast<int>(i);
    }
  }
  return static_cast<int>(icons.size()) - 1;
}

// Moves `value` towards `target` by `step`, stopping there.
float approach(const float value, const float target, const float step) {
  return value < target ? std::min(target, value + step) : std::max(target, value - step);
}

const DealerLook &look_of(const DealerScript &script, const std::string &dealer) {
  const auto found = script.dealers.find(dealer);
  if (found == script.dealers.end()) {
    throw std::runtime_error("dealer.json has no dealer \"" + dealer + "\"");
  }
  return found->second;
}

} // namespace

std::vector<DealerOffer> dealer_offers(const GarageTables &garage, const PartTables &parts,
                                       const int lineup, const float markup) {
  std::vector<DealerOffer> offers;
  if (lineup < 0 || lineup >= static_cast<int>(garage.dealer_lineups.size())) {
    return offers;
  }
  for (const LineupEntry &entry : garage.dealer_lineups[static_cast<std::size_t>(lineup)]) {
    if (const RobotRecord *robot = garage.robot(entry.robot)) {
      // The x87 multiplies the total by the float at full precision, then truncates.
      const double price =
          static_cast<double>(robot_price(parts, garage, *robot)) * static_cast<double>(markup);
      offers.push_back({*robot, entry.picture, static_cast<std::int64_t>(price)});
    }
  }
  return offers;
}

Dealer::Dealer(DealerScript script, const std::string &dealer, std::vector<DealerOffer> offers,
               QuestSave quest, const PartTables &parts, const GarageTables &garage,
               const std::string &from, const int resume_item)
    : TownLogic(script), script_(std::move(script)), look_(look_of(script_, dealer)),
      offers_(std::move(offers)), quest_(std::move(quest)),
      purchase_(script_.purchase, parts, garage),
      side_(script_.repeat_delay, script_.repeat_interval) {
  if (from == script_.purchase.spec_screen) {
    // Returning from the shop's spec panel is not a new store visit.
    band_ = menu_ = 1.0F;
  }
  const int count = static_cast<int>(script_.items.size());
  cursor_ = previous_ = std::clamp(resume_item, 0, count - 1);
  logo_ = chosen_offer() >= 0 ? 0.0F : 1.0F;
  trail_.assign(static_cast<std::size_t>(std::max(1, script_.logo.trail)), logo_);
  set_help(script_.help[item(cursor_).id]);
}

const ShopIcon &Dealer::item(const int index) const {
  const int offer = offer_of(index);
  if (offer >= 0) {
    const auto legs = offers_[static_cast<std::size_t>(offer)].robot.part(PartSlot::legs);
    const auto icon = legs ? look_.legs_icons.find(legs->id) : look_.legs_icons.end();
    return icon != look_.legs_icons.end() ? icon->second : look_.default_icon;
  }
  return script_.items[static_cast<std::size_t>(index)];
}

// 0x1fda60: the items between the profile and EXIT show the offers.
int Dealer::offer_of(const int item) const {
  const int offer = item - 1;
  const bool sv = script_.items[static_cast<std::size_t>(item)].id == "sv";
  return sv && offer >= 0 && offer < static_cast<int>(offers_.size()) ? offer : -1;
}

void Dealer::set_help(const std::string &text) {
  if (text != help_) {
    help_ = text;
    help_seconds_ = 0.0;
  }
}

DealerView Dealer::view() const noexcept {
  switch (purchase_.view()) {
  case PurchaseView::trade:
    return DealerView::trade;
  case PurchaseView::hangars:
    return DealerView::hangars;
  case PurchaseView::message:
    return DealerView::message;
  case PurchaseView::question:
    return DealerView::question;
  case PurchaseView::closed:
    break;
  }
  return view_;
}

void Dealer::open(const DealerView view) {
  view_ = view;
  view_seconds_ = 0.0;
  side_.reset();
  if (view == DealerView::menu) {
    set_help(script_.help[item(cursor_).id]);
  }
}

// The band, then the menu, slide in (0x3a990) and out (0x3a500); the name
// plate cross-fades and the logo moves with its trail (0x393ee).
void Dealer::animate(const double tick_seconds) {
  const auto band_step = static_cast<float>(tick_seconds / script_.band_seconds);
  const auto menu_step = static_cast<float>(tick_seconds / script_.menu_seconds);
  if (closing_) {
    if (menu_ > 0.0F) {
      menu_ = approach(menu_, 0.0F, menu_step);
    } else if (band_ > 0.0F) {
      band_ = approach(band_, 0.0F, band_step);
    } else if (!leaving()) {
      leave(close_target_);
    }
  } else if (band_ < 1.0F) {
    band_ = approach(band_, 1.0F, band_step);
  } else {
    menu_ = approach(menu_, 1.0F, menu_step);
  }
  picture_ = approach(picture_, 1.0F, static_cast<float>(tick_seconds / script_.picture_seconds));
  const float target = chosen_offer() >= 0 ? 0.0F : 1.0F;
  logo_ = approach(logo_, target, static_cast<float>(tick_seconds / script_.logo.move_seconds));
  trail_.push_front(logo_);
  trail_.pop_back();
}

void Dealer::update(const double tick_seconds, const InputFrame &raw_input) {
  const InputFrame input = pointer_input(raw_input);
  if (done()) {
    return;
  }
  view_seconds_ += tick_seconds;
  help_seconds_ += tick_seconds;
  const bool taking = advance(tick_seconds);
  animate(tick_seconds);
  if (!taking || closing_) {
    return;
  }
  if (band_ < 1.0F || menu_ < 1.0F) {
    // A press skips the opening.
    if (input.any_pressed(script_.confirm_buttons) || input.any_pressed(script_.cancel_buttons)) {
      band_ = menu_ = 1.0F;
    }
    return;
  }
  if (purchase_.open()) {
    purchase_input(tick_seconds, input);
    return;
  }
  if (view_ == DealerView::menu) {
    menu_input(tick_seconds, input);
  } else if (view_seconds_ >= script_.profile_growth.seconds &&
             (input.any_pressed(script_.confirm_buttons) ||
              input.any_pressed(script_.cancel_buttons))) {
    sound(script_.cancel_sound);
    open(DealerView::menu);
  }
}

void Dealer::purchase_input(const double tick_seconds, const InputFrame &input) {
  const PurchaseResult result = purchase_.update(tick_seconds, input, quest_);
  for (const std::string &name : purchase_.take_sounds()) {
    sound(name);
  }
  if (purchase_.open()) {
    set_help(purchase_.help());
  }
  switch (result) {
  case PurchaseResult::closed:
    open(DealerView::menu);
    break;
  case PurchaseResult::spec:
    // Keep the store open; its closing sequence displays the farewell.
    leave(script_.purchase.spec_screen);
    break;
  case PurchaseResult::name_entry:
    close_to("name_entry");
    break;
  case PurchaseResult::none:
    break;
  }
}

void Dealer::close_to(const std::string &target) {
  closing_ = true;
  close_target_ = target;
}

void Dealer::menu_input(const double tick_seconds, const InputFrame &input) {
  const int count = static_cast<int>(script_.items.size());
  bool fresh = false;
  const int step = side_.step(tick_seconds, input, script_.left, script_.right, fresh);
  if (const int moved = move_cursor(cursor_, step, fresh, count); moved != cursor_) {
    previous_ = cursor_;
    cursor_ = moved;
    picture_ = 0.0F;
    sound(script_.move_sound);
    set_help(script_.help[item(cursor_).id]);
    return;
  }
  const int exit = index_of(script_.items, "exit");
  if (input.any_pressed(script_.confirm_buttons)) {
    if (cursor_ == exit) {
      sound(script_.leave_sound);
      close_to(script_.exit_to);
    } else if (chosen_offer() >= 0) {
      sound(script_.select_sound);
      purchase_.start(offers_[static_cast<std::size_t>(chosen_offer())]);
      set_help(purchase_.help());
    } else {
      sound(script_.select_sound);
      open(DealerView::profile);
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons) && cursor_ != exit) {
    previous_ = cursor_;
    cursor_ = exit;
    picture_ = 0.0F;
    sound(script_.jump_sound);
    set_help(script_.help[item(cursor_).id]);
  }
}

} // namespace phantomcrash
