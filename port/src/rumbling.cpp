#include "phantomcrash/rumbling.hpp"

#include <algorithm>
#include <memory>
#include <random>
#include <utility>

namespace phantomcrash {
namespace {

float progress(const double seconds, const double span) {
  return span > 0.0 ? static_cast<float>(std::clamp(seconds / span, 0.0, 1.0)) : 1.0F;
}

RosterRandom roster_random() {
  auto engine = std::make_shared<std::minstd_rand>(std::random_device{}());
  return [engine] { return std::uniform_real_distribution<float>(0.0F, 1.0F)(*engine); };
}

} // namespace

Rumbling::Rumbling(RumblingScript script, const ContestTables &contests, RegulationCheck check,
                   QuestSave quest, const std::optional<int> resume_area)
    : TownLogic(script), script_(std::move(script)), contests_(contests), check_(check),
      quest_(std::move(quest)), across_(script_.repeat_delay, script_.repeat_interval),
      rows_(script_.repeat_delay, script_.repeat_interval) {
  fade_seconds_ = script_.area_select.fade_seconds;
  refresh_rosters(check_, contests_, quest_, roster_random());
  const int exit = static_cast<int>(script_.areas.size()) - 1;
  if (resume_area && *resume_area >= 0 && *resume_area < exit) {
    // 0x016d0e, back from the hangar: the area's sub-menu on SELECT SV.
    area_ = faded_area_ = *resume_area;
    open_seconds_ = script_.area_select.slide_seconds + script_.area_select.bar_slide_seconds;
    turn_seconds_ = script_.menu.turn_seconds;
    item_ = static_cast<int>(RumblingAction::select_sv);
    view_ = RumblingView::menu;
    const ContestClass *rules = contest_class();
    regulation_met_ = rules != nullptr && meets_regulation(check_, *rules, quest_);
  }
  refresh_help();
}

int Rumbling::contest(const int area) const {
  if (area < 0 || area >= static_cast<int>(script_.areas.size()) ||
      script_.areas[static_cast<std::size_t>(area)].exit) {
    return no_contest;
  }
  return contest_on(contests_, quest_.day, area, quest_.cleared);
}

bool Rumbling::won(const int contest) const {
  return std::find(quest_.won_contests.begin(), quest_.won_contests.end(), contest) !=
         quest_.won_contests.end();
}

const ContestClass *Rumbling::contest_class() const {
  const Contest *held = contests_.contest(contest(area_));
  return held != nullptr ? contests_.contest_class(held->contest_class) : nullptr;
}

float Rumbling::area_fade() const {
  return progress(fade_seconds_, script_.area_select.fade_seconds);
}

float Rumbling::items_slide() const {
  return progress(open_seconds_, script_.area_select.slide_seconds);
}

float Rumbling::bar_slide() const {
  return progress(open_seconds_, script_.area_select.bar_slide_seconds);
}

float Rumbling::turn() const {
  return progress(turn_seconds_, script_.menu.turn_seconds);
}

bool Rumbling::in_menu() const noexcept {
  return view_ == RumblingView::menu || view_ == RumblingView::gates ||
         view_ == RumblingView::character_info || view_ == RumblingView::contest_info ||
         (view_ == RumblingView::notice && back_view_ == RumblingView::menu);
}

int Rumbling::gate_count() const {
  return static_cast<int>(script_.areas[static_cast<std::size_t>(area_)].gates.size());
}

// 0x014edc greys ENTRY without a Scoobee ridden that meets the regulation,
// or in a rentals-only contest, and RENTAL where rentals are not allowed.
// (The original tests for class 0x19 itself, the one class whose rule is
// rentals only; testing the rule keeps a new rentals-only class greyed.)
// CHARACTER INFO is never greyed.
bool Rumbling::can_choose(const RumblingAction action) const {
  const ContestClass *rules = contest_class();
  switch (action) {
  case RumblingAction::entry:
    return rules != nullptr && (rules->other != class_rentals_only || check_.open_contests) &&
           regulation_met_;
  case RumblingAction::rental: {
    const Contest *held = contests_.contest(contest(area_));
    return held != nullptr &&
           (check_.open_contests || rental_allowed(check_.rules, held->contest_class));
  }
  default:
    return true;
  }
}

// 0x01435e.
Rumbling::AreaResult Rumbling::area_result() const {
  if (script_.areas[static_cast<std::size_t>(area_)].exit) {
    return AreaResult::leave;
  }
  const int held = contest(area_);
  const ContestClass *rules = contest_class();
  if (held == no_contest || rules == nullptr) {
    return AreaResult::closed;
  }
  const RankerContest ranker = ranker_contest(check_.rules, area_, held);
  if (!check_.open_contests && !ranker_open(check_.rules, ranker, area_, quest_.won_contests)) {
    return ranker == RankerContest::area ? AreaResult::area_ranker : AreaResult::first_ranker;
  }
  return quest_.money < rules->fee ? AreaResult::poor : AreaResult::open;
}

void Rumbling::set_help(const std::string &text) {
  if (help_ != text) {
    help_ = text;
    help_seconds_ = 0.0;
  }
}

void Rumbling::refresh_help() {
  switch (view_) {
  case RumblingView::areas:
  case RumblingView::turning_out:
    set_help(script_.areas[static_cast<std::size_t>(area_)].help);
    break;
  case RumblingView::turning_in:
  case RumblingView::menu:
  case RumblingView::character_info: // 0x01671e leaves the item's help up
    set_help(script_.menu.items[static_cast<std::size_t>(item_)].help);
    break;
  case RumblingView::gates:
    set_help(script_.gates.help);
    break;
  case RumblingView::contest_info:
    set_help(script_.contest_info.help);
    break;
  case RumblingView::notice:
    break;
  }
}

void Rumbling::say(const std::string &message, const RumblingView back) {
  message_ = message;
  back_view_ = back;
  view_ = RumblingView::notice;
}

void Rumbling::update(const double tick_seconds, const InputFrame &input) {
  if (done()) {
    return;
  }
  help_seconds_ += tick_seconds;
  open_seconds_ += tick_seconds;
  fade_seconds_ += tick_seconds;
  menu_seconds_ += tick_seconds;
  info_seconds_ += tick_seconds;
  if (view_ == RumblingView::turning_in) {
    turn_seconds_ = std::min(turn_seconds_ + tick_seconds, script_.menu.turn_seconds);
    if (turn_seconds_ >= script_.menu.turn_seconds) {
      view_ = RumblingView::menu;
      menu_seconds_ = 0.0;
    }
  } else if (view_ == RumblingView::turning_out) {
    turn_seconds_ = std::max(turn_seconds_ - tick_seconds, 0.0);
    if (turn_seconds_ <= 0.0) {
      view_ = RumblingView::areas;
    }
  }
  if (!advance(tick_seconds)) {
    return;
  }
  switch (view_) {
  case RumblingView::areas:
    areas_input(tick_seconds, input);
    break;
  case RumblingView::menu:
    menu_input(tick_seconds, input);
    break;
  case RumblingView::gates:
    gates_input(tick_seconds, input);
    break;
  case RumblingView::character_info:
    character_input(tick_seconds, input);
    break;
  case RumblingView::contest_info:
    if (input.any_pressed(script_.cancel_buttons)) {
      sound(script_.close_sound);
      view_ = RumblingView::menu;
    }
    break;
  case RumblingView::notice:
    if (input.any_pressed(script_.confirm_buttons)) {
      sound(script_.close_sound);
      message_.clear();
      view_ = back_view_;
    }
    break;
  default:
    break;
  }
  refresh_help();
}

void Rumbling::move_area(const int area) {
  if (area == area_) {
    return;
  }
  faded_area_ = area_;
  area_ = area;
  fade_seconds_ = 0.0;
}

// 0x0163e7: left and right choose the area (sys 4, wrapping); cancel moves
// to EXIT (sys 4).
void Rumbling::areas_input(const double tick_seconds, const InputFrame &input) {
  const int count = static_cast<int>(script_.areas.size());
  bool fresh = false;
  if (const int step = across_.step(tick_seconds, input, script_.left, script_.right, fresh)) {
    const int moved = move_cursor(area_, step, fresh, count);
    if (moved != area_) {
      sound(script_.move_sound);
      move_area(moved);
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    if (area_ != count - 1) {
      sound(script_.to_exit_sound);
      move_area(count - 1);
    }
    return;
  }
  if (!input.any_pressed(script_.confirm_buttons)) {
    return;
  }
  switch (area_result()) {
  case AreaResult::leave:
    sound(script_.select_sound);
    leave(script_.exit_to);
    break;
  case AreaResult::closed:
    sound(script_.no_sound);
    break;
  case AreaResult::poor:
    sound(script_.no_sound);
    say("fee", RumblingView::areas);
    break;
  case AreaResult::area_ranker:
    sound(script_.no_sound);
    say("area_ranker", RumblingView::areas);
    break;
  case AreaResult::first_ranker:
    sound(script_.no_sound);
    say("first_ranker", RumblingView::areas);
    break;
  case AreaResult::open: {
    // 0x014895: the regulation is checked as the sub-menu opens.
    sound(script_.select_sound);
    item_ = 0;
    const ContestClass *rules = contest_class();
    regulation_met_ = rules != nullptr && meets_regulation(check_, *rules, quest_);
    view_ = RumblingView::turning_in;
    break;
  }
  }
}

// 0x01671e: up and down choose (sys 3); cancel moves to BACK (sys 4).
void Rumbling::menu_input(const double tick_seconds, const InputFrame &input) {
  const int count = static_cast<int>(script_.menu.items.size());
  bool fresh = false;
  if (const int step = rows_.step(tick_seconds, input, script_.up, script_.down, fresh)) {
    const int moved = std::clamp(item_ + step, 0, count - 1);
    if (moved != item_) {
      sound(script_.row_sound);
      item_ = moved;
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    if (item_ != count - 1) {
      sound(script_.to_exit_sound);
      item_ = count - 1;
    }
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    choose(script_.menu.items[static_cast<std::size_t>(item_)].action);
  }
}

void Rumbling::choose(const RumblingAction action) {
  const ContestClass *rules = contest_class();
  switch (action) {
  case RumblingAction::entry:
    if (rules != nullptr && rules->other != class_rentals_only && quest_.garage.riding < 0) {
      sound(script_.no_sound);
      say("no_robot", RumblingView::menu);
    } else if (can_choose(action)) {
      sound(script_.select_sound);
      open_gates(false);
    } else {
      sound(script_.no_sound);
      const bool rentals = rules != nullptr && rules->other == class_rentals_only;
      say(rentals ? "rentals_only" : "regulation", RumblingView::menu);
    }
    break;
  case RumblingAction::rental:
    if (can_choose(action)) {
      sound(script_.select_sound);
      open_gates(true);
    } else {
      sound(script_.no_sound);
      say("no_rentals", RumblingView::menu);
    }
    break;
  case RumblingAction::select_sv:
    sound(script_.select_sound);
    leave(script_.hangar_route);
    break;
  case RumblingAction::character_info:
    // 0x01671e: open with someone out in the area, or in a ranker's contest.
    cards_ = wire_heads_cards(check_, contests_, quest_, area_, contest(area_));
    if (cards_.empty()) {
      sound(script_.no_sound);
      break;
    }
    sound(script_.select_sound);
    card_ = 0;
    info_seconds_ = 0.0;
    view_ = RumblingView::character_info;
    break;
  case RumblingAction::contest_info:
    sound(script_.select_sound);
    info_seconds_ = 0.0;
    view_ = RumblingView::contest_info;
    break;
  case RumblingAction::back:
    view_ = RumblingView::turning_out;
    break;
  }
}

// 0x015aa2: left and right step through the opponents (sys 3, wrapping);
// cancel goes back to the sub-menu (sys 8).
void Rumbling::character_input(const double tick_seconds, const InputFrame &input) {
  const int count = static_cast<int>(cards_.size());
  bool fresh = false;
  if (const int step = across_.step(tick_seconds, input, script_.left, script_.right, fresh)) {
    const int moved = move_cursor(card_, step, fresh, count);
    if (moved != card_) {
      sound(script_.browse_sound);
      card_ = moved;
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    sound(script_.no_sound);
    view_ = RumblingView::menu;
  }
}

} // namespace phantomcrash
