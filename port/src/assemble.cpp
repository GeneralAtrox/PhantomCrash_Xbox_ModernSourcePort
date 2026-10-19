#include "phantomcrash/assemble.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {
namespace {

float progress(const double seconds, const double length) {
  return static_cast<float>(std::clamp(seconds / length, 0.0, 1.0));
}

} // namespace

Assemble::Assemble(AssembleScript script, QuestSave quest, const PartTables &parts,
                   const GarageTables &garage, const PartStatsTable &table, const int hangar,
                   const std::optional<AssembleItem> resume)
    : TownLogic(script), script_(std::move(script)), quest_(std::move(quest)), parts_(parts),
      garage_(garage), table_(table), hangar_(std::clamp(hangar, 0, garage_hangar_count - 1)),
      repeat_(script_.repeat_delay, script_.repeat_interval),
      across_(script_.repeat_delay, script_.repeat_interval) {
  if (resume) {
    if (const AssembleCell *found = script_.cell_for(*resume)) {
      const auto index = static_cast<int>(found - script_.cells.data());
      column_ = index / script_.grid_rows;
      row_ = index % script_.grid_rows;
    }
  }
  settle();
  refresh_help();
}

const Hangar &Assemble::fitted() const {
  return quest_.garage.hangars[static_cast<std::size_t>(hangar_)];
}

const AssembleCell &Assemble::cell() const {
  return script_.cells[static_cast<std::size_t>(column_ * script_.grid_rows + row_)];
}

FittingRules Assemble::rules() const {
  return {script_.same_maker, script_.unpaintable_makers};
}

// 0x019a3e: NAME and SPEC need a body, legs and CHIP, DIS something to
// take off, COLOR a robot that can be painted.
bool Assemble::can_choose(const AssembleItem item) const {
  switch (item) {
  case AssembleItem::name:
  case AssembleItem::spec:
    return status_.complete;
  case AssembleItem::dismantle:
    return !status_.empty;
  case AssembleItem::paint:
    return std::ranges::find(script_.unpaintable_makers,
                             robot_maker(parts_, quest_.garage, fitted())) ==
           script_.unpaintable_makers.end();
  default:
    return true;
  }
}

float Assemble::slide() const {
  return progress(seconds(), script_.slide_seconds);
}

bool Assemble::can_fit(const ShopGoods &line) const {
  return phantomcrash::can_fit(parts_, garage_, quest_.garage, hangar_, line, side_, rules());
}

bool Assemble::fitted_here(const ShopGoods &line) const {
  return fitted_in(quest_.garage, hangar_, line);
}

RobotTotals Assemble::totals() const {
  return robot_totals(table_, parts_, garage_, quest_.garage, fitted());
}

RobotTotals Assemble::totals_with(const ShopGoods &line) const {
  Garage trial = quest_.garage;
  fit(parts_, garage_, trial, hangar_, line, side_);
  return robot_totals(table_, parts_, garage_, trial,
                      trial.hangars[static_cast<std::size_t>(hangar_)]);
}

void Assemble::settle() {
  settle_hangar(parts_, rules(), quest_.garage, hangar_, quest_.riding);
  status_ = hangar_status(parts_, quest_.garage, hangar_, script_.overweight_margin);
}

void Assemble::refresh_help() {
  std::string text = cell().help;
  if (view_ == AssembleView::sides && cell().sides) {
    text = cell().sides->help[static_cast<std::size_t>(side_)];
  } else if (view_ == AssembleView::list) {
    text = script_.list_help;
  } else if (view_ == AssembleView::details) {
    text = script_.details_help;
  }
  if (text != help_) {
    help_ = std::move(text);
    help_seconds_ = 0.0;
  }
}

void Assemble::update(const double tick_seconds, const InputFrame &raw_input) {
  const InputFrame input = pointer_input(raw_input);
  if (done()) {
    return;
  }
  help_seconds_ += tick_seconds;
  sides_seconds_ += tick_seconds;
  list_seconds_ += tick_seconds;
  details_seconds_ += tick_seconds;
  if (!advance(tick_seconds)) {
    return;
  }
  switch (view_) {
  case AssembleView::grid:
    grid_input(tick_seconds, input);
    break;
  case AssembleView::sides:
    sides_input(tick_seconds, input);
    break;
  case AssembleView::list:
    list_input(tick_seconds, input);
    break;
  case AssembleView::details:
    details_input(input);
    break;
  case AssembleView::question:
    question_input(tick_seconds, input);
    break;
  case AssembleView::notice:
    if (input.any_pressed(script_.confirm_buttons)) {
      message_.clear();
      view_ = AssembleView::list;
      refresh_help();
    }
    break;
  }
}

// 0x019a3e with 0x017060: left/right (sys 4) and up/down (sys 3) through
// the grid, cancel to EXIT (sys 4); confirm on a cell that cannot be
// chosen is refused (sys 8).
void Assemble::grid_input(const double tick_seconds, const InputFrame &input) {
  bool fresh = false;
  if (const int step = across_.step(tick_seconds, input, script_.left, script_.right, fresh)) {
    if (const int moved = move_cursor(column_, step, fresh, script_.columns); moved != column_) {
      column_ = moved;
      sound(script_.move_sound);
      refresh_help();
    }
    return;
  }
  if (const int step = repeat_.step(tick_seconds, input, script_.up, script_.down, fresh)) {
    if (const int moved = move_cursor(row_, step, fresh, script_.grid_rows); moved != row_) {
      row_ = moved;
      sound(script_.row_sound);
      refresh_help();
    }
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    choose_cell();
    return;
  }
  if (hide_toggle(input)) {
    return;
  }
  if (input.any_pressed(script_.cancel_buttons) && cell().item != AssembleItem::exit) {
    const auto index =
        static_cast<int>(script_.cell_for(AssembleItem::exit) - script_.cells.data());
    column_ = index / script_.grid_rows;
    row_ = index % script_.grid_rows;
    sound(script_.exit_sound);
    refresh_help();
  }
}

void Assemble::choose_cell() {
  const AssembleCell &chosen = cell();
  if (!can_choose(chosen.item)) {
    sound(script_.back_sound);
    return;
  }
  sound(script_.select_sound);
  switch (chosen.item) {
  case AssembleItem::exit:
    leave(script_.exit_to);
    return;
  case AssembleItem::dismantle:
    view_ = AssembleView::question;
    choice_ = 1; // NO
    message_ = "dismantle";
    sound(script_.question_sound);
    return;
  default:
    break;
  }
  if (const auto route = script_.routes.find(chosen.item); route != script_.routes.end()) {
    if (chosen.item == AssembleItem::name) {
      naming_hangar_ = hangar_;
    }
    leave(route->second);
    return;
  }
  if (!chosen.kind) {
    return;
  }
  side_ = 0;
  if (chosen.sides) {
    view_ = AssembleView::sides;
    sides_seconds_ = 0.0;
    refresh_help();
    return;
  }
  open_list();
}

// 0x01b78f: left and right choose the side on screen (sys 4); confirm
// opens the list, cancel goes back to the grid.
void Assemble::sides_input(const double tick_seconds, const InputFrame &input) {
  bool fresh = false;
  if (const int step = across_.step(tick_seconds, input, script_.left, script_.right, fresh)) {
    if (const int moved = step < 0 ? 1 : 0; moved != side_) {
      side_ = moved;
      sound(script_.move_sound);
      refresh_help();
    }
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    sound(script_.select_sound);
    open_list();
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    sound(script_.back_sound);
    view_ = AssembleView::grid;
    refresh_help();
  }
}

// 0x0182b1: the stock's parts of the kind, or the paints and emblems owned,
// by name.
void Assemble::open_list() {
  list_.assign(goods_owned(table_, parts_, garage_, quest_.garage, *cell().kind, 1.0), true);
  filter_list();
  in_list_ = true;
  view_ = AssembleView::list;
  list_seconds_ = 0.0;
  repeat_.reset();
  refresh_help();
}

void Assemble::close_list() {
  in_list_ = false;
  view_ = cell().sides ? AssembleView::sides : AssembleView::grid;
  sides_seconds_ = cell().sides ? cell().sides->seconds : 0.0;
  refresh_help();
}

// 0x01b59d: up/down through the lines (sys 18), X and Y sort (sys 5),
// cancel goes back; confirm opens a line that can be fitted (else sys 8).
void Assemble::list_input(const double tick_seconds, const InputFrame &input) {
  bool fresh = false;
  if (const int step = repeat_.step(tick_seconds, input, script_.up, script_.down, fresh)) {
    if (list_.move(step, script_.list.rows)) {
      sound(script_.list_row_sound);
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    sound(script_.back_sound);
    close_list();
    return;
  }
  if (hide_toggle(input)) {
    rebuild_list();
    return;
  }
  if (list_paging(input)) {
    return;
  }
  if (input.any_pressed(script_.sort_name) || input.any_pressed(script_.sort_price)) {
    list_.sort(input.any_pressed(script_.sort_name) ? 0 : 1);
    sound(script_.sort_sound);
    return;
  }
  if (!input.any_pressed(script_.confirm_buttons)) {
    return;
  }
  const ShopGoods *line = list_.chosen();
  if (line == nullptr || !can_fit(*line)) {
    sound(script_.back_sound);
    return;
  }
  sound(script_.select_sound);
  view_ = AssembleView::details;
  details_seconds_ = 0.0;
  details_page_ = false;
  refresh_help();
}

// 0x01b482: confirm fits the line (sys 29), cancel goes back to the list.
void Assemble::details_input(const InputFrame &input) {
  if (turn_details(input)) {
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    fit_chosen();
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    sound(script_.back_sound);
    view_ = AssembleView::list;
    refresh_help();
  }
}

void Assemble::fit_chosen() {
  const ShopGoods *line = list_.chosen();
  if (line == nullptr) {
    return;
  }
  const FitResult result = fit(parts_, garage_, quest_.garage, hangar_, *line, side_);
  sound(script_.fit_sound);
  list_.refit(quest_.garage);
  filter_list();
  settle();
  view_ = AssembleView::list;
  if (result.modules_off > 0) {
    message_ = "modules_off";
    view_ = AssembleView::notice;
  }
  refresh_help();
}

// 0x01ade0: YES takes everything off (sys 41), NO goes back (sys 5);
// cancel moves to NO.
void Assemble::question_input(const double tick_seconds, const InputFrame &input) {
  if (input.dialog_choice) {
    choice_ = std::clamp(*input.dialog_choice, 0, 1);
  }
  bool fresh = false;
  if (const int step = across_.step(tick_seconds, input, script_.left, script_.right, fresh)) {
    if (const int moved = std::clamp(choice_ + step, 0, 1); moved != choice_) {
      choice_ = moved;
      sound(script_.move_sound);
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons) && choice_ != 1) {
    choice_ = 1;
    sound(script_.no_sound);
    return;
  }
  if (!input.any_pressed(script_.confirm_buttons)) {
    return;
  }
  if (choice_ == 0) {
    dismantle(quest_.garage, hangar_);
    sound(script_.dismantle_sound);
    settle();
  } else {
    sound(script_.select_sound);
  }
  message_.clear();
  view_ = AssembleView::grid;
  refresh_help();
}

} // namespace phantomcrash
