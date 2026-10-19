#include "phantomcrash/used_sv.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {

std::vector<SvOffer> used_sv_offers(const UsedSvScript &script, const GarageTables &garage,
                                    const PartTables &parts, const int lineup,
                                    const std::vector<int> &won_contests) {
  std::vector<SvOffer> offers;
  if (lineup < 0 || lineup >= static_cast<int>(garage.used_sv_lineups.size())) {
    return offers;
  }
  const bool unlocked = std::find(won_contests.begin(), won_contests.end(),
                                  script.unlock_contest) != won_contests.end();
  const auto max_slots = script.slots.size() - 1;
  for (const int id : garage.used_sv_lineups[static_cast<std::size_t>(lineup)]) {
    const RobotRecord *robot = garage.robot(id);
    if (robot == nullptr || offers.size() >= max_slots) {
      continue;
    }
    const auto body = robot->part(PartSlot::body);
    int maker = -1;
    if (body && body->id >= 0 && body->id < static_cast<int>(parts.bodies.size())) {
      maker = parts.bodies[static_cast<std::size_t>(body->id)].maker;
    }
    if (maker < script.gated_maker || unlocked) {
      offers.push_back({*robot, -1, robot_price(parts, garage, *robot), maker});
    }
  }
  return offers;
}

UsedSv::UsedSv(UsedSvScript script, std::vector<SvOffer> offers, QuestSave quest,
               const PartTables &parts, const GarageTables &garage, const int resume_item)
    : TownLogic(script), script_(std::move(script)), offers_(std::move(offers)),
      quest_(std::move(quest)), purchase_(script_.purchase, parts, garage),
      side_(script_.repeat_delay, script_.repeat_interval) {
  cursor_ = std::clamp(resume_item, 0, item_count() - 1);
  menu_help();
}

int UsedSv::slot_of(const int item) const noexcept {
  return static_cast<int>(script_.slots.size()) - item_count() + item;
}

const ShopIcon &UsedSv::icon(const int item) const {
  const int offer = offer_of(item);
  if (offer < 0) {
    return script_.exit;
  }
  const auto legs = offers_[static_cast<std::size_t>(offer)].robot.part(PartSlot::legs);
  const auto found = legs ? script_.legs_icons.find(legs->id) : script_.legs_icons.end();
  return found != script_.legs_icons.end() ? found->second : script_.default_icon;
}

float UsedSv::slot_x(const int slot) const {
  const auto index = static_cast<std::size_t>(slot);
  const float from = script_.slot_from[index];
  return std::min(script_.slots[index][0],
                  from + script_.slot_speed * static_cast<float>(seconds()));
}

float UsedSv::strip_alpha() const {
  return static_cast<float>(std::min(1.0, seconds() / script_.strip_seconds));
}

float UsedSv::plate_offset() const {
  const double sliding = (seconds() - script_.strip_seconds) / script_.plate_seconds;
  return script_.plate_from * (1.0F - static_cast<float>(std::clamp(sliding, 0.0, 1.0)));
}

bool UsedSv::opened() const {
  return seconds() >= script_.strip_seconds + script_.plate_seconds;
}

float UsedSv::gear_angle() const {
  return static_cast<float>(seconds()) * script_.gear_spin;
}

void UsedSv::set_help(const std::string &text) {
  if (text != help_) {
    help_ = text;
    help_seconds_ = 0.0;
  }
}

void UsedSv::menu_help() {
  const auto found = script_.help.find(offer_of(cursor_) >= 0 ? "sv" : "exit");
  set_help(found != script_.help.end() ? found->second : std::string{});
}

void UsedSv::update(const double tick_seconds, const InputFrame &raw_input) {
  const InputFrame input = pointer_input(raw_input);
  if (done()) {
    return;
  }
  help_seconds_ += tick_seconds;
  const bool taking = advance(tick_seconds);
  if (!opening_sound_ && seconds() >= script_.strip_seconds) {
    opening_sound_ = true;
    sound(script_.open_sound);
  }
  // 0x230f4: the panel grows while an SV is chosen and shrinks on EXIT.
  const float target = opened() && offer_of(cursor_) >= 0 ? 1.0F : 0.0F;
  const auto step = static_cast<float>(tick_seconds / script_.info_growth.seconds);
  info_ = target > info_ ? std::min(target, info_ + step) : std::max(target, info_ - step);
  if (!taking || !opened()) {
    return;
  }
  if (purchase_.open()) {
    purchase_input(tick_seconds, input);
  } else {
    menu_input(tick_seconds, input);
  }
}

// 0x27910: left/right over the SVs and EXIT; confirm opens the trade window
// or leaves; cancel jumps to EXIT.
void UsedSv::menu_input(const double tick_seconds, const InputFrame &input) {
  const SvPurchaseScript &controls = script_.purchase;
  bool fresh = false;
  const int step = side_.step(tick_seconds, input, controls.left, controls.right, fresh);
  if (const int moved = move_cursor(cursor_, step, fresh, item_count()); moved != cursor_) {
    cursor_ = moved;
    sound(script_.move_sound);
    menu_help();
    return;
  }
  const int exit = item_count() - 1;
  if (input.any_pressed(script_.confirm_buttons)) {
    sound(script_.select_sound);
    if (cursor_ == exit) {
      leave(script_.exit_to);
    } else {
      purchase_.start(offers_[static_cast<std::size_t>(cursor_)]);
      set_help(purchase_.help());
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons) && cursor_ != exit) {
    cursor_ = exit;
    sound(controls.jump_sound);
    menu_help();
  }
}

void UsedSv::purchase_input(const double tick_seconds, const InputFrame &input) {
  const PurchaseResult result = purchase_.update(tick_seconds, input, quest_);
  for (const std::string &name : purchase_.take_sounds()) {
    sound(name);
  }
  if (purchase_.open()) {
    set_help(purchase_.help());
  }
  if (result == PurchaseResult::closed) {
    side_.reset();
    menu_help();
  } else if (result == PurchaseResult::spec) {
    leave(script_.purchase.spec_screen);
  } else if (result == PurchaseResult::name_entry) {
    leave("name_entry");
  }
}

} // namespace phantomcrash
