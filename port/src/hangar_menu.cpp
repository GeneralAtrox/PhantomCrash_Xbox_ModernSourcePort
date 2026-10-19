#include "phantomcrash/hangar_menu.hpp"

#include "phantomcrash/robot_load.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace phantomcrash {
namespace {

// What a hangar lacking a body, legs or CHIP cannot do: only ASSEMBLE and
// CLOSE are offered (0x01e4d3).
bool needs_complete(const HangarAction action) {
  return action != HangarAction::assemble && action != HangarAction::close;
}

// The first fitted part's maker (0x0ba401): the body's, else the legs',
// else a weapon's.
int loadout_maker(const PartTables &parts, const RobotLoadout &loadout) {
  const auto armour = [](const std::vector<ArmourRecord> &records, const int index) {
    return index >= 0 && index < static_cast<int>(records.size())
               ? records[static_cast<std::size_t>(index)].maker
               : -1;
  };
  if (const int maker = armour(parts.bodies, loadout.body); maker >= 0) {
    return maker;
  }
  if (const int maker = armour(parts.legs, loadout.legs); maker >= 0) {
    return maker;
  }
  for (const PartRef &weapon : loadout.weapons) {
    if (const WeaponRecord *record = parts.weapon(weapon)) {
      return record->maker;
    }
  }
  return -1;
}

} // namespace

HangarStatus hangar_status(const PartTables &parts, const Garage &garage, const int hangar,
                           const float margin) {
  HangarStatus status;
  if (hangar < 0 || hangar >= garage_hangar_count) {
    return status;
  }
  const Hangar &bay = garage.hangars[static_cast<std::size_t>(hangar)];
  status.owned = bay.owned;
  if (!status.owned) {
    return status;
  }
  status.riding = garage.riding == hangar;
  status.empty = bay.empty();
  status.complete = bay.complete();
  status.unarmed =
      std::ranges::none_of(std::array{PartSlot::right_arm, PartSlot::left_arm,
                                      PartSlot::right_shoulder, PartSlot::left_shoulder},
                           [&bay](const PartSlot slot) { return bay.part(slot) >= 0; });
  const RobotLoadout loadout = hangar_loadout(garage, hangar);
  const LoadStats load = load_stats(parts, loadout);
  status.overweight = load.weight - load.capacity >= margin;
  status.maker = loadout_maker(parts, loadout);
  return status;
}

HangarMenu::HangarMenu(HangarMenuScript script, QuestSave quest, const PartTables &parts,
                       const int hangar, const std::optional<HangarAction> resume)
    : TownLogic(script), script_(std::move(script)), quest_(std::move(quest)), parts_(parts),
      across_(script_.repeat_delay, script_.repeat_interval),
      repeat_(script_.repeat_delay, script_.repeat_interval),
      card_(std::clamp(hangar, 0, exit_card)), scroll_seconds_(script_.cards.scroll_seconds) {
  refresh_status();
  if (resume && card_ < exit_card && status(card_).owned) {
    const auto &items = script_.menu.items;
    const auto found = std::find_if(items.begin(), items.end(), [&](const HangarMenuItem &item) {
      return item.action == *resume;
    });
    open_menu(static_cast<int>(found - items.begin()));
    menu_seconds_ = script_.menu.window.seconds;
  }
  refresh_help();
}

const HangarStatus &HangarMenu::status(const int hangar) const {
  static const HangarStatus none{};
  return hangar >= 0 && hangar < garage_hangar_count ? statuses_[static_cast<std::size_t>(hangar)]
                                                     : none;
}

void HangarMenu::refresh_status() {
  for (int hangar = 0; hangar < garage_hangar_count; ++hangar) {
    statuses_[static_cast<std::size_t>(hangar)] =
        hangar_status(parts_, quest_.garage, hangar, script_.overweight_margin);
  }
}

float HangarMenu::wrapped(const float x) const {
  const HangarCards &cards = script_.cards;
  if (x > cards.reach) {
    return x - cards.period;
  }
  if (x < -cards.reach) {
    return x + cards.period;
  }
  return x;
}

// 0x01d0df: the chosen card comes to the centre from where it was as the
// wheel scrolls, the others following it `step` apart to its right, wrapped.
float HangarMenu::card_x(const int card) const {
  const HangarCards &cards = script_.cards;
  const double t = std::clamp(scroll_seconds_ / cards.scroll_seconds, 0.0, 1.0);
  const float base = cards.centre_x + scroll_from_ * static_cast<float>(1.0 - t);
  const int along = ((card - card_) % card_count + card_count) % card_count;
  return wrapped(base + cards.step * static_cast<float>(along));
}

float HangarMenu::plates_slide() const {
  return static_cast<float>(std::clamp(seconds() / script_.plates.slide_seconds, 0.0, 1.0));
}

int HangarMenu::close_item() const {
  return static_cast<int>(script_.menu.items.size()) - 1;
}

int HangarMenu::first_item() const {
  const auto &items = script_.menu.items;
  for (std::size_t index = 0; index < items.size(); ++index) {
    if (can_choose(items[index].action)) {
      return static_cast<int>(index);
    }
  }
  return close_item();
}

HangarAction HangarMenu::item_action(const int item) const {
  return script_.menu.items[static_cast<std::size_t>(item)].action;
}

bool HangarMenu::can_choose(const HangarAction action) const {
  const HangarStatus &bay = status(card_);
  if (!bay.complete && needs_complete(action)) {
    return false;
  }
  return action != HangarAction::test_drive || bay.riding;
}

void HangarMenu::refresh_help() {
  std::string text;
  if (view_ == HangarView::buy) {
    text = script_.buy_help[static_cast<std::size_t>(choice_)];
  } else if (in_menu_) {
    text = script_.menu.items[static_cast<std::size_t>(item_)].help;
  } else {
    text = card_ == exit_card ? script_.exit_help : script_.hangar_help;
  }
  if (text != help_) {
    help_ = std::move(text);
    help_seconds_ = 0.0;
  }
}

void HangarMenu::update(const double tick_seconds, const InputFrame &raw_input) {
  if (done()) {
    return;
  }
  help_seconds_ += tick_seconds;
  scroll_seconds_ += tick_seconds;
  menu_seconds_ += tick_seconds;
  if (!advance(tick_seconds)) {
    return;
  }
  const InputFrame input = pointer_input(raw_input);
  switch (view_) {
  case HangarView::wheel:
    wheel_input(tick_seconds, input);
    break;
  case HangarView::menu:
    menu_input(tick_seconds, input);
    break;
  case HangarView::buy:
    buy_input(tick_seconds, input);
    break;
  case HangarView::notice:
    if (input.any_pressed(script_.confirm_buttons)) {
      if (std::ranges::find(script_.board_notices, message_) != script_.board_notices.end()) {
        sound(script_.board_sound);
      }
      // 0x01e2d1: the Scoobee is boarded once its notice is dismissed.
      if (boarding_ >= 0) {
        quest_.garage.riding = boarding_;
        quest_.riding = quest_.garage.hangars[static_cast<std::size_t>(boarding_)].name;
        boarding_ = -1;
        refresh_status();
      }
      message_.clear();
      view_ = back_to_menu_ ? HangarView::menu : HangarView::wheel;
      in_menu_ = back_to_menu_;
      refresh_help();
    }
    break;
  }
}

// The wheel scrolls the new card to the centre from where it is now.
void HangarMenu::choose_card(const int card) {
  const float from = card_x(card) - script_.cards.centre_x;
  card_ = card;
  scroll_from_ = wrapped(from);
  scroll_seconds_ = 0.0;
  refresh_help();
}

// 0x01e81b: left/right along the wheel (sys 3 and 28), X to the ridden hangar,
// cancel to EXIT (sys 8 and 28); confirm leaves on EXIT, offers a hangar
// not owned for sale and opens an owned one's menu; an origin with refusals
// (KEEP SV) turns away what it will not take instead.
void HangarMenu::wheel_input(const double tick_seconds, const InputFrame &input) {
  bool fresh = false;
  if (const int step = across_.step(tick_seconds, input, script_.left, script_.right, fresh)) {
    choose_card((card_ + step + card_count) % card_count);
    sound(script_.move_sound);
    sound(script_.turn_sound);
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    sound(script_.select_sound);
    if (card_ == exit_card) {
      leave(script_.exit_to);
    } else if (script_.refuse && !(status(card_).owned && status(card_).complete)) {
      say(script_.refuse->incomplete, false);
    } else if (script_.refuse && status(card_).unarmed) {
      say(script_.refuse->unarmed, false);
    } else if (!status(card_).owned) {
      view_ = HangarView::buy;
      choice_ = 1; // NO
      message_ = "buy";
      refresh_help();
    } else {
      open_menu(first_item());
      menu_seconds_ = 0.0;
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons) && card_ != exit_card) {
    sound(script_.back_sound);
    sound(script_.exit_sound);
    choose_card(exit_card);
    return;
  }
  const int riding = quest_.garage.riding;
  if (input.any_pressed(script_.riding) && riding >= 0 && riding < garage_hangar_count &&
      riding != card_) {
    choose_card(riding);
  }
}

void HangarMenu::open_menu(const int item) {
  in_menu_ = true;
  view_ = HangarView::menu;
  item_ = std::clamp(item, 0, close_item());
  if (!can_choose(item_action(item_))) {
    item_ = first_item();
  }
  refresh_help();
}

// 0x01e4d3: up/down through what the hangar can do (sys 3), stepping over
// TEST DRIVE unless it is the ridden hangar; cancel moves to CLOSE (sys 3).
void HangarMenu::menu_input(const double tick_seconds, const InputFrame &input) {
  bool fresh = false;
  if (const int step = repeat_.step(tick_seconds, input, script_.up, script_.down, fresh)) {
    int moved = item_ + step;
    while (moved >= 0 && moved <= close_item() && !can_choose(item_action(moved))) {
      moved += step;
    }
    if (moved >= 0 && moved <= close_item() && moved != item_) {
      item_ = moved;
      sound(script_.row_sound);
      refresh_help();
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    if (item_ != close_item()) {
      item_ = close_item();
      sound(script_.close_sound);
      refresh_help();
    }
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    act(item_action(item_));
  }
}

void HangarMenu::act(const HangarAction action) {
  sound(script_.select_sound);
  switch (action) {
  case HangarAction::ride:
    if (status(card_).riding) {
      say("ridden", true);
      return;
    }
    boarding_ = card_;
    say("boarded", false);
    return;
  case HangarAction::keep:
    kept_ = card_;
    break;
  case HangarAction::close:
    in_menu_ = false;
    view_ = HangarView::wheel;
    refresh_help();
    return;
  default:
    break;
  }
  if (const auto route = script_.routes.find(action); route != script_.routes.end()) {
    leave(route->second);
  }
}

void HangarMenu::say(const std::string &message, const bool in_menu) {
  message_ = message;
  view_ = HangarView::notice;
  back_to_menu_ = in_menu;
  in_menu_ = in_menu;
}

// 0x01e706: YES buys the hangar or says the money will not do; NO and
// cancel (moving to NO, sys 4) go back to the wheel.
void HangarMenu::buy_input(const double tick_seconds, const InputFrame &input) {
  if (input.dialog_choice) {
    choice_ = std::clamp(*input.dialog_choice, 0, 1);
    refresh_help();
  }
  bool fresh = false;
  if (const int step = across_.step(tick_seconds, input, script_.left, script_.right, fresh)) {
    if (const int moved = std::clamp(choice_ + step, 0, 1); moved != choice_) {
      choice_ = moved;
      sound(script_.move_sound);
      refresh_help();
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons) && choice_ != 1) {
    choice_ = 1;
    sound(script_.no_sound);
    refresh_help();
    return;
  }
  if (!input.any_pressed(script_.confirm_buttons)) {
    return;
  }
  sound(script_.select_sound);
  if (choice_ == 0) {
    buy();
    return;
  }
  message_.clear();
  view_ = HangarView::wheel;
  refresh_help();
}

void HangarMenu::buy() {
  if (quest_.money < script_.hangar_price) {
    say("poor", false);
    return;
  }
  quest_.money -= script_.hangar_price;
  quest_.garage.hangars[static_cast<std::size_t>(card_)].owned = true;
  sound(script_.trade_sound);
  refresh_status();
  say("thanks", false);
}

} // namespace phantomcrash
