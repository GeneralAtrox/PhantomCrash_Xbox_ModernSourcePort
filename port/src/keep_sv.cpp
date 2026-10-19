#include "phantomcrash/keep_sv.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {
namespace {

enum Column { list_column, save_column, exit_column, column_count };

// Past the whole opening: coming back skips it (0x04373d's 75 frames).
constexpr double intro_done_seconds = 1e6;

} // namespace

KeepSv::KeepSv(KeepSvScript script, const PartTables &parts, QuestSave quest,
               std::vector<VsScoobee> &kept, KeepSvCursor &cursor, KeepSvEntry entry)
    : script_(std::move(script)), parts_(parts), quest_(std::move(quest)), kept_(kept),
      cursor_(cursor), fade_(script_.fade_in, script_.fade_out),
      across_(script_.repeat_delay, script_.repeat_interval),
      down_(script_.repeat_delay, script_.repeat_interval),
      row_({script_.slots.visible, script_.slots.scroll_seconds, script_.slots.step * 0.5F}) {
  if (entry.fresh) {
    cursor_ = KeepSvCursor{};
  } else {
    intro_seconds_ = intro_done_seconds;
  }
  cursor_.first = std::clamp(cursor_.first, 0, count() - script_.slots.visible);
  cursor_.row = std::clamp(cursor_.row, 0, script_.slots.visible - 1);
  cursor_.column = std::clamp(cursor_.column, 0, column_count - 1);
  to_column();
  if (entry.kept) {
    keep(std::move(*entry.kept));
  }
}

int KeepSv::count() const {
  return std::max(static_cast<int>(kept_.size()), script_.slots.visible);
}

const VsScoobee *KeepSv::scoobee(const int index) const {
  if (index < 0 || index >= static_cast<int>(kept_.size())) {
    return nullptr;
  }
  return &kept_[static_cast<std::size_t>(index)];
}

bool KeepSv::damaged(const int index) const {
  const VsScoobee *kept = scoobee(index);
  return kept != nullptr && vs_scoobee_damaged(*kept, parts_);
}

int KeepSv::maker(const int index) const {
  const VsScoobee *kept = scoobee(index);
  return kept != nullptr ? vs_scoobee_maker(*kept, parts_) : -1;
}

int KeepSv::shown_first() const {
  return row_.shown_first(cursor_.first);
}

bool KeepSv::shows_details() const {
  if (view_ == KeepSvView::save || view_ == KeepSvView::exit) {
    return false;
  }
  if (cursor_.column != list_column || pending_ || message_ == "saved") {
    return false;
  }
  return scoobee(chosen()) != nullptr && !damaged(chosen());
}

const std::vector<KeepSvPopupOption> *KeepSv::popup() const {
  const auto found = script_.popups.find(popup_);
  return view_ == KeepSvView::popup && found != script_.popups.end() ? &found->second : nullptr;
}

void KeepSv::update(const double tick_seconds, const InputFrame &input) {
  seconds_ += tick_seconds;
  intro_seconds_ += tick_seconds;
  row_.update(tick_seconds);
  if (!fade_.update(tick_seconds) || fade_.leaving()) {
    return;
  }
  switch (view_) {
  case KeepSvView::list:
    list_input(tick_seconds, input);
    break;
  case KeepSvView::save:
  case KeepSvView::exit:
    column_input(tick_seconds, input);
    break;
  case KeepSvView::popup:
    popup_input(tick_seconds, input);
    break;
  case KeepSvView::question:
    question_input(tick_seconds, input);
    break;
  case KeepSvView::notice:
    if (input.any_pressed(script_.confirm_buttons)) {
      sounds_.play(script_.select_sound);
      to_column();
    }
    break;
  }
}

// 0x042f2d state 0: left/right along the list (sys 19); nothing else while
// it scrolls.
void KeepSv::list_input(const double tick_seconds, const InputFrame &input) {
  if (scrolling()) {
    return;
  }
  bool fresh = false;
  if (const int step = across_.step(tick_seconds, input, script_.left, script_.right, fresh)) {
    move_row(step);
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    confirm_row();
    return;
  }
  column_input(tick_seconds, input);
}

// Up/down between the list, SAVE and EXIT (sys 3); cancel moves to EXIT
// (sys 3); confirm on SAVE or EXIT.
void KeepSv::column_input(const double tick_seconds, const InputFrame &input) {
  bool fresh = false;
  if (const int step = down_.step(tick_seconds, input, script_.up, script_.down, fresh)) {
    const int column = std::clamp(cursor_.column + step, 0, column_count - 1);
    if (column != cursor_.column) {
      cursor_.column = column;
      sounds_.play(script_.column_sound);
      to_column();
    }
    return;
  }
  if (view_ != KeepSvView::list && input.any_pressed(script_.confirm_buttons)) {
    sounds_.play(script_.select_sound);
    if (view_ == KeepSvView::exit) {
      fade_.leave(script_.exit_to);
    } else {
      confirm_save();
    }
    return;
  }
  if (view_ != KeepSvView::exit && input.any_pressed(script_.cancel_buttons)) {
    cursor_.column = exit_column;
    sounds_.play(script_.to_exit_sound);
    to_column();
  }
}

// The cursor moves along the visible rows; past either end the list
// scrolls by one while there are rows that way.
void KeepSv::move_row(const int step) {
  if (row_.move(cursor_.first, cursor_.row, step, count())) {
    sounds_.play(script_.move_sound);
  }
}

// An empty row buzzes (sys 8); a kept Scoobee asks what to do with it,
// a damaged one only offering DELETE.
void KeepSv::confirm_row() {
  if (scoobee(chosen()) == nullptr) {
    sounds_.play(script_.no_sound);
    return;
  }
  sounds_.play(script_.select_sound);
  open_popup(damaged(chosen()) ? "damaged" : "choose");
}

// 0x042f2d state 1: MAKE is offered while some hangar holds a complete Scoobee.
void KeepSv::confirm_save() {
  if (can_save()) {
    open_popup("save");
  } else {
    tell("none");
  }
}

bool KeepSv::can_save() const {
  return std::ranges::any_of(quest_.garage.hangars,
                             [](const Hangar &bay) { return bay.owned && bay.complete(); });
}

// Left/right choose (sys 4); cancel moves to CANCEL (sys 4).
void KeepSv::popup_input(const double tick_seconds, const InputFrame &input) {
  const std::vector<KeepSvPopupOption> &options = *popup();
  const int last = static_cast<int>(options.size()) - 1;
  bool fresh = false;
  if (const int step = across_.step(tick_seconds, input, script_.left, script_.right, fresh)) {
    const int moved = std::clamp(choice_ + step, 0, last);
    if (moved != choice_) {
      choice_ = moved;
      sounds_.play(script_.option_sound);
    }
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    sounds_.play(script_.select_sound);
    choose(options[static_cast<std::size_t>(choice_)].option);
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    choice_ = last;
    sounds_.play(script_.to_cancel_sound);
  }
}

void KeepSv::choose(const KeepSvOption option) {
  switch (option) {
  case KeepSvOption::spec:
    fade_.leave(script_.spec_route);
    break;
  case KeepSvOption::remove:
    ask("delete", 1);
    break;
  case KeepSvOption::make:
    fade_.leave(script_.make_route);
    break;
  case KeepSvOption::cancel:
    to_column();
    break;
  }
}

// YES/NO (sys 4), NO chosen by cancel (sys 4).
void KeepSv::question_input(const double tick_seconds, const InputFrame &input) {
  bool fresh = false;
  if (const int step = across_.step(tick_seconds, input, script_.left, script_.right, fresh)) {
    const int moved = std::clamp(choice_ + step, 0, 1);
    if (moved != choice_) {
      choice_ = moved;
      sounds_.play(script_.option_sound);
    }
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    sounds_.play(script_.select_sound);
    answer(choice_ == 0);
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    choice_ = 1;
    sounds_.play(script_.to_cancel_sound);
  }
}

// DELETE removes the Scoobee, the list stepping back to stay full
// (0x042f2d state 8); OVERWRITE puts the new one in the old one's place.
void KeepSv::answer(const bool yes) {
  if (message_ == "overwrite") {
    VsScoobee scoobee = *std::exchange(pending_, std::nullopt);
    if (!yes) {
      to_column();
      return;
    }
    const auto same = std::ranges::find(kept_, scoobee.name, &VsScoobee::name);
    *same = std::move(scoobee);
    tell("saved");
    return;
  }
  if (yes && scoobee(chosen()) != nullptr) {
    kept_.erase(kept_.begin() + chosen());
    if (cursor_.first > 0 && cursor_.first + script_.slots.visible > count()) {
      --cursor_.first;
      row_.scroll(-1);
    }
  }
  to_column();
}

void KeepSv::ask(const std::string &message, const int choice) {
  view_ = KeepSvView::question;
  message_ = message;
  choice_ = choice;
}

void KeepSv::open_popup(const std::string &popup) {
  view_ = KeepSvView::popup;
  popup_ = popup;
  message_ = popup;
  choice_ = 0;
}

void KeepSv::tell(const std::string &message) {
  view_ = KeepSvView::notice;
  message_ = message;
}

void KeepSv::to_column() {
  static constexpr KeepSvView views[column_count]{KeepSvView::list, KeepSvView::save,
                                                  KeepSvView::exit};
  view_ = views[cursor_.column];
  message_.clear();
  popup_.clear();
  across_.reset();
}

// 0x04373d: coming back with the hangar's Scoobee saves it at the end, or
// asks first when one of its name is kept.
void KeepSv::keep(VsScoobee scoobee) {
  if (std::ranges::find(kept_, scoobee.name, &VsScoobee::name) != kept_.end()) {
    pending_ = std::move(scoobee);
    ask("overwrite", 1);
    return;
  }
  kept_.push_back(std::move(scoobee));
  tell("saved");
}

} // namespace phantomcrash
