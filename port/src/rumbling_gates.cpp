// RUMBLING's gates: choosing the gate to come in by, and entering.

#include "phantomcrash/rumbling.hpp"

#include <algorithm>
#include <random>

namespace phantomcrash {

// The ranker of a ranker's contest waits at a gate the player cannot take
// (0x016d0e picks it at random for the day; the port from the day).
void Rumbling::open_gates(const bool rental) {
  rental_ = rental;
  const int count = gate_count();
  ranker_gate_ = -1;
  if (count > 1 && ranker_contest(check_.rules, area_, contest(area_)) != RankerContest::none) {
    ranker_gate_ = (quest_.day + area_) % count;
  }
  gate_ = std::clamp(gate_, 0, std::max(count - 1, 0));
  if (gate_ == ranker_gate_) {
    gate_ = (gate_ + 1) % count;
  }
  view_ = RumblingView::gates;
}

// 0x0169e5: left and right choose the gate (sys 4, wrapping, past the
// ranker's); confirm enters, cancel goes back to the sub-menu (sys 8).
void Rumbling::gates_input(const double tick_seconds, const InputFrame &input) {
  const int count = gate_count();
  bool fresh = false;
  if (const int step = across_.step(tick_seconds, input, script_.left, script_.right, fresh)) {
    int moved = move_cursor(gate_, step, fresh, count);
    if (moved == ranker_gate_) {
      const int past = move_cursor(moved, step, fresh, count);
      moved = past == ranker_gate_ ? gate_ : past;
    }
    if (moved != gate_) {
      sound(script_.move_sound);
      gate_ = moved;
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    sound(script_.no_sound);
    view_ = RumblingView::menu;
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    sound(script_.select_sound);
    enter();
  }
}

// 0x0169e5: the fee is paid (0x0b1ebb) and a rental SV picked at random.
void Rumbling::enter() {
  RumblingEntry entry;
  entry.area = area_;
  entry.contest = contest(area_);
  entry.gate = gate_;
  entry.rental = rental_;
  entry.ranker_gate = ranker_gate_;
  if (rental_ && !check_.rules.rental_robots.empty()) {
    std::minstd_rand random{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> pick(0, check_.rules.rental_robots.size() - 1);
    entry.rental_robot = check_.rules.rental_robots[pick(random)];
  }
  if (const ContestClass *rules = contest_class()) {
    quest_.money -= rules->fee;
  }
  entry_ = entry;
  leave(script_.battle_route);
}

} // namespace phantomcrash
