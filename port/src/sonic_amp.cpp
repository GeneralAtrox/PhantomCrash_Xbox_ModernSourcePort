#include "phantomcrash/sonic_amp.hpp"

#include <algorithm>
#include <numeric>
#include <utility>

namespace phantomcrash {
namespace {

std::string replaced(std::string text, const std::string &key, const std::string &value) {
  if (const auto at = text.find(key); at != std::string::npos) {
    text.replace(at, key.size(), value);
  }
  return text;
}

int index_of(const std::vector<ShopIcon> &icons, const std::string &id) {
  for (std::size_t i = 0; i < icons.size(); ++i) {
    if (icons[i].id == id) {
      return static_cast<int>(i);
    }
  }
  return static_cast<int>(icons.size()) - 1;
}

} // namespace

SonicAmp::SonicAmp(SonicAmpScript script, MusicLibrary music, const std::int64_t money,
                   std::vector<MusicCredit> credits, const std::string &from)
    : TownLogic(script), script_(std::move(script)), music_(std::move(music)), money_(money),
      credits_(std::move(credits)), repeat_(script_.repeat_delay, script_.repeat_interval),
      side_(script_.repeat_delay, script_.repeat_interval) {
  own_music_ = town_music(script_, from);
  restart_help();
}

std::string SonicAmp::music_track() const {
  if (preview_ >= 0) {
    return credits_[static_cast<std::size_t>(preview_)].file;
  }
  return own_music_;
}

int SonicAmp::chart_track(const int rank) const {
  if (rank < 0 || rank >= static_cast<int>(music_.chart.size())) {
    return -1;
  }
  const int track = music_.chart[static_cast<std::size_t>(rank)];
  return track >= 0 && track < static_cast<int>(credits_.size()) ? track : -1;
}

int SonicAmp::columns() const {
  return spot() ? script_.grid.spot_columns : script_.grid.catalogue_columns;
}

int SonicAmp::first_page() const {
  const auto &pages = script_.grid.category_pages;
  const auto category = static_cast<std::size_t>(std::max(0, item().category));
  return std::accumulate(pages.begin(), pages.begin() + std::min(category, pages.size()), 0);
}

int SonicAmp::pages() const {
  const auto &pages = script_.grid.category_pages;
  const int category = item().category;
  if (spot() || category < 0 || category >= static_cast<int>(pages.size())) {
    return 1;
  }
  return pages[static_cast<std::size_t>(category)];
}

int SonicAmp::rows() const {
  const int per_row = columns();
  if (spot()) {
    return (music_chart_size + per_row - 1) / per_row;
  }
  const int sold = std::min(script_.grid.catalogue_tracks, static_cast<int>(credits_.size()));
  const int left = sold - (first_page() + page_) * per_row * script_.grid.catalogue_rows;
  return std::clamp((left + per_row - 1) / per_row, 1, script_.grid.catalogue_rows);
}

int SonicAmp::slot_track(const int row, const int column) const {
  if (spot()) {
    return chart_track(row * script_.grid.spot_columns + column);
  }
  const int per_page = script_.grid.catalogue_columns * script_.grid.catalogue_rows;
  const int track =
      (first_page() + page_) * per_page + row * script_.grid.catalogue_columns + column;
  const int sold = std::min(script_.grid.catalogue_tracks, static_cast<int>(credits_.size()));
  if (track >= sold) {
    return -1;
  }
  const auto &ids = script_.grid.catalogue_ids;
  const int credit = ids.empty() ? track : ids[static_cast<std::size_t>(track)];
  return credit >= 0 && credit < static_cast<int>(credits_.size()) ? credit : -1;
}

void SonicAmp::restart_help() {
  std::string text = script_.help.text;
  const int track = view_ == SonicAmpView::menu ? -1 : cursor_track();
  if (track >= 0) {
    const MusicCredit &credit = credits_[static_cast<std::size_t>(track)];
    text = replaced(replaced(script_.track_format, "{title}", credit.title), "{artist}",
                    credit.artist);
  }
  if (text != help_) {
    help_ = text;
    help_seconds_ = 0.0;
  }
}

void SonicAmp::open(const SonicAmpView view) {
  view_ = view;
  view_seconds_ = 0.0;
  repeat_.reset();
  side_.reset();
  restart_help();
}

void SonicAmp::update(const double tick_seconds, const InputFrame &raw_input) {
  const InputFrame input = pointer_input(raw_input);
  if (done()) {
    return;
  }
  view_seconds_ += tick_seconds;
  grid_seconds_ += tick_seconds;
  help_seconds_ += tick_seconds;
  // 0x2b1ec: the chart counts down from No. 10, a rank every chart.seconds.
  rank_seconds_ += tick_seconds;
  if (rank_seconds_ >= script_.chart.seconds) {
    rank_seconds_ -= script_.chart.seconds;
    rank_ = rank_ == 0 ? music_chart_size - 1 : rank_ - 1;
  }
  if (!advance(tick_seconds)) {
    return;
  }
  if (!opened_) {
    opened_ = true;
    sound(script_.open_sound);
  }
  switch (view_) {
  case SonicAmpView::menu:
    menu_input(tick_seconds, input);
    break;
  case SonicAmpView::browse:
    browse_input(tick_seconds, input);
    break;
  case SonicAmpView::actions:
    actions_input(tick_seconds, input);
    break;
  case SonicAmpView::message:
    // 0x2ae92: shown a while, or until confirmed.
    if (view_seconds_ >= script_.message_seconds || input.any_pressed(script_.confirm_buttons)) {
      saying_.clear();
      open(SonicAmpView::browse);
    }
    break;
  case SonicAmpView::preview:
    if (view_seconds_ >= script_.preview_min_seconds && input.any_pressed(script_.cancel_buttons)) {
      sound(script_.cancel_sound);
      preview_ = -1;
      open(SonicAmpView::browse);
    }
    break;
  }
}

void SonicAmp::menu_input(const double tick_seconds, const InputFrame &input) {
  const int count = static_cast<int>(script_.items.size());
  bool fresh = false;
  const int step = side_.step(tick_seconds, input, script_.left, script_.right, fresh);
  if (const int moved = move_cursor(cursor_, step, fresh, count); moved != cursor_) {
    cursor_ = moved;
    sound(script_.move_sound);
    return;
  }
  const int exit = index_of(script_.items, "exit");
  if (input.any_pressed(script_.confirm_buttons)) {
    if (cursor_ == exit) {
      sound(script_.leave_sound);
      leave(script_.exit_to);
      return;
    }
    sound(script_.select_sound);
    page_ = column_ = row_ = 0;
    grid_seconds_ = 0.0;
    open(SonicAmpView::browse);
    return;
  }
  // Cancel moves the cursor to EXIT.
  if (input.any_pressed(script_.cancel_buttons) && cursor_ != exit) {
    cursor_ = exit;
    sound(script_.jump_sound);
  }
}

void SonicAmp::turn_page(const int step) {
  if (pages() < 2) {
    return;
  }
  page_ = (page_ + step + pages()) % pages();
  row_ = std::min(row_, rows() - 1);
  sound(script_.jump_sound);
}

void SonicAmp::browse_input(const double tick_seconds, const InputFrame &input) {
  if (input.any_pressed(script_.cancel_buttons)) {
    sound(script_.cancel_sound);
    open(SonicAmpView::menu);
    return;
  }
  if (input.any_pressed(script_.page_back)) {
    turn_page(-1);
  } else if (input.any_pressed(script_.page_forward)) {
    turn_page(1);
  } else if (input.any_pressed(script_.confirm_buttons)) {
    if (cursor_track() >= 0) {
      sound(script_.select_sound);
      action_ = script_.first_action;
      saying_ = "price";
      open(SonicAmpView::actions);
      return;
    }
  } else {
    bool fresh = false;
    const int step = side_.step(tick_seconds, input, script_.left, script_.right, fresh);
    const int moved = move_cursor(column_, step, fresh, columns());
    if (moved != column_) {
      // A catalogue turns its page off either edge.
      if (fresh && pages() > 1 && (step < 0) == (moved > column_)) {
        turn_page(step);
      } else {
        sound(script_.move_sound);
      }
      column_ = moved;
    }
  }
  bool fresh = false;
  const int step = repeat_.step(tick_seconds, input, script_.up, script_.down, fresh);
  if (const int moved = move_cursor(row_, step, fresh, rows()); moved != row_) {
    row_ = moved;
    sound(script_.move_sound);
  }
  restart_help();
}

void SonicAmp::buy() {
  const int track = cursor_track();
  if (music_.owns(track)) {
    saying_ = "owned";
    sound(script_.select_sound);
  } else if (money_ >= script_.price) {
    money_ -= script_.price;
    music_.owned[track] = TrackState::fresh;
    saying_ = "thanks";
    sound(script_.buy_sound);
  } else {
    saying_ = "poor";
    sound(script_.select_sound);
  }
  open(SonicAmpView::message);
}

void SonicAmp::actions_input(const double tick_seconds, const InputFrame &input) {
  const int cancel = index_of(script_.actions, "cancel");
  if (input.any_pressed(script_.cancel_buttons)) {
    if (action_ != cancel) {
      action_ = cancel;
      sound(script_.jump_sound);
    }
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    const std::string &id = script_.actions[static_cast<std::size_t>(action_)].id;
    if (id == "buy") {
      buy();
    } else if (id == "preview") {
      sound(script_.select_sound);
      preview_ = cursor_track();
      saying_.clear();
      open(SonicAmpView::preview);
    } else {
      sound(script_.cancel_sound);
      saying_.clear();
      open(SonicAmpView::browse);
    }
    return;
  }
  bool fresh = false;
  const int step = side_.step(tick_seconds, input, script_.left, script_.right, fresh);
  const int count = static_cast<int>(script_.actions.size());
  if (const int moved = move_cursor(action_, step, fresh, count); moved != action_) {
    action_ = moved;
    sound(script_.move_sound);
  }
}

} // namespace phantomcrash
