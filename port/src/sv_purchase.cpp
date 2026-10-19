// Buying an SV into a hangar: the trade window, the hangars, the notices
// and the question, shared by the dealers and Wild Machines' used SVs.

#include "phantomcrash/sv_purchase.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
namespace {

using json::member;
using json::numbers;
using json::required;

float number_of(const json::JsonValue &value, const char *key) {
  return static_cast<float>(required(value, key).number());
}

int integer_of(const json::JsonValue &value, const char *key) {
  return static_cast<int>(required(value, key).number());
}

std::uint32_t colour_of(const json::JsonValue &value, const char *key) {
  return parse_colour(required(value, key).string());
}

HangarCarousel parse_hangars(const json::JsonValue &value) {
  HangarCarousel hangars;
  hangars.panel = member(value, "panel", parse_sprite);
  const auto arrows = member(value, "arrows", parse_sprites);
  if (arrows.size() != hangars.arrows.size()) {
    throw std::runtime_error("hangars: arrows needs the left and the right one");
  }
  hangars.arrows = {arrows[0], arrows[1]};
  hangars.arrow_lit = colour_of(value, "arrow_lit");
  hangars.numbers = member(value, "numbers", parse_hangar_labels);
  hangars.centre = numbers<2>(required(value, "centre"), "centre");
  hangars.step = number_of(value, "step");
  hangars.clip = numbers<4>(required(value, "clip"), "clip");
  hangars.empty_colour = colour_of(value, "empty_colour");
  hangars.used_colour = colour_of(value, "used_colour");
  hangars.chosen_empty = colour_of(value, "chosen_empty");
  hangars.chosen_used = colour_of(value, "chosen_used");
  hangars.number_layer = integer_of(value, "number_layer");
  hangars.chosen_layer = integer_of(value, "chosen_layer");
  hangars.scroll_seconds = required(value, "scroll_seconds").number();
  hangars.shown = integer_of(value, "shown");
  if (value.find("cancel_hint") != nullptr) {
    hangars.cancel_hint = member(value, "cancel_hint", parse_sprites);
  }
  if (hangars.numbers.size() != static_cast<std::size_t>(garage_hangar_count) ||
      hangars.scroll_seconds <= 0.0) {
    throw std::runtime_error("hangars: one number a hangar, and scroll_seconds above 0");
  }
  return hangars;
}

} // namespace

void parse_sv_purchase(const json::JsonValue &document, const MenuBasics &basics,
                       SvPurchaseScript &script) {
  const auto &buttons = required(document, "buttons");
  script.left = buttons_from_json(required(buttons, "left"));
  script.right = buttons_from_json(required(buttons, "right"));
  script.confirm = basics.confirm_buttons;
  script.cancel = basics.cancel_buttons;
  script.repeat_delay = basics.repeat_delay;
  script.repeat_interval = basics.repeat_interval;
  script.move_sound = basics.move_sound;
  script.select_sound = basics.select_sound;
  const auto &sounds = required(document, "sounds");
  script.jump_sound = sounds.string_or("jump", "");
  script.cancel_sound = sounds.string_or("cancel", "");
  script.buy_sound = sounds.string_or("buy", "");
  const auto &trade = required(document, "trade");
  script.trade_window = member(trade, "window", parse_sprite);
  script.actions = member(trade, "actions", parse_shop_icons);
  script.spec_screen = required(trade, "spec_screen").string();
  script.trade_help = parse_lines(required(document, "trade_help"));
  script.hangars = member(document, "hangars", parse_hangars);
  script.message_text_at = numbers<2>(required(document, "message_text_at"), "message_text_at");
  for (const auto &[id, lines] : required(document, "messages").object()) {
    script.messages[id] = parse_lines(lines);
  }
  if (script.actions.size() != 3 || script.trade_help.size() != script.actions.size()) {
    throw std::runtime_error("trade: three actions, each with its help line");
  }
}

SvPurchase::SvPurchase(SvPurchaseScript script, const PartTables &parts, const GarageTables &garage)
    : script_(std::move(script)), parts_(&parts), garage_(&garage),
      side_(script_.repeat_delay, script_.repeat_interval) {}

std::vector<std::string> SvPurchase::take_sounds() {
  return std::exchange(sounds_, {});
}

std::string SvPurchase::help() const {
  if (view_ == PurchaseView::trade) {
    return script_.trade_help[static_cast<std::size_t>(action_)];
  }
  return view_ == PurchaseView::hangars ? script_.hangar_help : std::string{};
}

void SvPurchase::start(const SvOffer &offer) {
  offer_ = offer;
  action_ = 0;
  message_.clear();
  show(PurchaseView::trade);
}

void SvPurchase::show(const PurchaseView view) {
  view_ = view;
  view_seconds_ = 0.0;
  side_.reset();
}

void SvPurchase::say(const std::string &message, const PurchaseView after) {
  message_ = message;
  after_message_ = after;
  show(PurchaseView::message);
}

PurchaseResult SvPurchase::update(const double tick_seconds, const InputFrame &raw_input,
                                  QuestSave &quest) {
  const InputFrame input = pointer_input(raw_input);
  if (input.dialog_choice) {
    choice_ = *input.dialog_choice;
  }
  view_seconds_ += tick_seconds;
  const float step = static_cast<float>(tick_seconds / script_.hangars.scroll_seconds);
  scroll_ = std::min(1.0F, scroll_ + step);
  switch (view_) {
  case PurchaseView::trade:
    return trade_input(tick_seconds, input, quest);
  case PurchaseView::hangars:
    hangar_input(tick_seconds, input, quest);
    return PurchaseResult::none;
  case PurchaseView::message:
    return message_input(input);
  case PurchaseView::question:
    return question_input(tick_seconds, input, quest);
  case PurchaseView::closed:
    break;
  }
  return PurchaseResult::none;
}

PurchaseResult SvPurchase::trade_input(const double tick_seconds, const InputFrame &input,
                                       const QuestSave &quest) {
  const int count = static_cast<int>(script_.actions.size());
  bool fresh = false;
  const int step = side_.step(tick_seconds, input, script_.left, script_.right, fresh);
  if (const int moved = move_cursor(action_, step, fresh, count); moved != action_) {
    action_ = moved;
    sounds_.push_back(script_.jump_sound);
    return PurchaseResult::none;
  }
  const int cancel = count - 1;
  if (input.any_pressed(script_.confirm)) {
    sounds_.push_back(script_.select_sound);
    const std::string &id = script_.actions[static_cast<std::size_t>(action_)].id;
    if (id == "spec") {
      return PurchaseResult::spec;
    }
    if (id == "buy") {
      buy(quest);
      return open() ? PurchaseResult::none : PurchaseResult::closed;
    }
    view_ = PurchaseView::closed;
    return PurchaseResult::closed;
  }
  if (input.any_pressed(script_.cancel) && action_ != cancel) {
    action_ = cancel;
    sounds_.push_back(script_.move_sound);
  }
  return PurchaseResult::none;
}

// 0x039501, 0x01cd33: an owned, empty hangar, and room in the stock for
// every part and module of a SV that can be ridden.
bool SvPurchase::fits(const QuestSave &quest, const int hangar) const {
  const Hangar &bay = quest.garage.hangars[static_cast<std::size_t>(hangar)];
  return bay.owned && bay.empty() && offer_.robot.complete() &&
         offer_.robot.stock_needed() <= quest.garage.free_stock();
}

// 0x3a618, 0x2605f: to the hangars when one fits and the money is there;
// else why not.
void SvPurchase::buy(const QuestSave &quest) {
  bool any = false;
  for (int hangar = 0; hangar < garage_hangar_count; ++hangar) {
    any = any || fits(quest, hangar);
  }
  if (offer_.price > quest.money) {
    say("poor", PurchaseView::closed);
  } else if (quest.garage.free_stock() == 0) {
    say("no_stock", PurchaseView::closed);
  } else if (!any) {
    say("no_hangar", PurchaseView::closed);
  } else {
    hangar_ = previous_hangar_ = 0;
    scroll_ = 1.0F;
    show(PurchaseView::hangars);
  }
}

void SvPurchase::hangar_input(const double tick_seconds, const InputFrame &input,
                              QuestSave &quest) {
  bool fresh = false;
  const int step = side_.step(tick_seconds, input, script_.left, script_.right, fresh);
  if (step != 0 && scroll_ >= 1.0F) {
    previous_hangar_ = hangar_;
    hangar_ = (hangar_ + step + garage_hangar_count) % garage_hangar_count;
    scroll_ = 0.0F;
    sounds_.push_back(script_.jump_sound);
    return;
  }
  if (input.any_pressed(script_.cancel)) {
    sounds_.push_back(script_.cancel_sound);
    show(PurchaseView::trade);
    return;
  }
  if (!input.any_pressed(script_.confirm) || scroll_ < 1.0F) {
    return;
  }
  sounds_.push_back(script_.select_sound);
  if (!fits(quest, hangar_)) {
    const bool owned = quest.garage.hangars[static_cast<std::size_t>(hangar_)].owned;
    say(owned ? "in_use" : "not_yours", PurchaseView::hangars);
    return;
  }
  // 0x3a2fe, 0x25126: fits the SV (its CHIP named after its record), pays.
  const int chip = install_robot(quest.garage, hangar_, offer_.robot, *parts_, *garage_);
  if (chip < 0) {
    say("no_stock", PurchaseView::closed);
    return;
  }
  quest.money -= offer_.price;
  sounds_.push_back(script_.buy_sound);
  bought_hangar_ = hangar_;
  bought_chip_ = chip;
  say("delivered", PurchaseView::closed);
}

// Boarded or not, the CHIP is named next where the script asks for it.
PurchaseResult SvPurchase::after_boarding() {
  if (script_.messages.count("name_chip") != 0) {
    say("name_chip", PurchaseView::closed);
    return PurchaseResult::none;
  }
  message_.clear();
  view_ = PurchaseView::closed;
  return PurchaseResult::closed;
}

// 0x3abda, 0x27ae2: a notice closes on confirm and leads on.
PurchaseResult SvPurchase::message_input(const InputFrame &input) {
  if (!input.any_pressed(script_.confirm)) {
    return PurchaseResult::none;
  }
  sounds_.push_back(script_.select_sound);
  if (message_ == "name_chip") {
    // 0x2c5d9 mode 2: NAME ENTRY names the CHIP's stock entry, then back.
    naming_stock_ = bought_chip_;
    message_.clear();
    view_ = PurchaseView::closed;
    return PurchaseResult::name_entry;
  }
  if (message_ == "boarded") {
    return after_boarding();
  }
  if (message_ == "delivered") {
    message_ = "board";
    choice_ = 1; // NO
    show(PurchaseView::question);
    return PurchaseResult::none;
  }
  message_.clear();
  show(after_message_);
  return open() ? PurchaseResult::none : PurchaseResult::closed;
}

// 0x3ad84: YES rides the SV bought.
PurchaseResult SvPurchase::question_input(const double tick_seconds, const InputFrame &input,
                                          QuestSave &quest) {
  bool fresh = false;
  const int step = side_.step(tick_seconds, input, script_.left, script_.right, fresh);
  if (const int moved = move_cursor(choice_, step, fresh, 2); moved != choice_) {
    choice_ = moved;
    sounds_.push_back(script_.move_sound);
    return PurchaseResult::none;
  }
  if (input.any_pressed(script_.cancel) && choice_ != 1) {
    choice_ = 1;
    sounds_.push_back(script_.move_sound);
    return PurchaseResult::none;
  }
  if (!input.any_pressed(script_.confirm)) {
    return PurchaseResult::none;
  }
  sounds_.push_back(script_.select_sound);
  if (choice_ == 0) {
    quest.garage.riding = bought_hangar_;
    quest.riding = quest.garage.hangars[static_cast<std::size_t>(bought_hangar_)].name;
    say("boarded", PurchaseView::closed);
    return PurchaseResult::none;
  }
  return after_boarding();
}

} // namespace phantomcrash
