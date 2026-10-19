#include "phantomcrash/name_entry.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace phantomcrash {
namespace {

// The original steps its animation once a frame at 60 Hz.
constexpr double original_rate = 60.0;
// Ticks of 1/60 s do not sum exactly.
constexpr double time_epsilon = 1e-9;
constexpr const char *space = "\xe3\x80\x80"; // U+3000, the game's space

bool is_space(const std::string &character) {
  return character == space || character == " ";
}

double rise(const double value, const double per_second, const double tick_seconds) {
  return std::min(1.0, value + per_second * tick_seconds);
}

} // namespace

NameEntry::NameEntry(NameEntryScript script, MessageBoxStyle box, QuestSave save)
    : script_(std::move(script)), box_(std::move(box)), save_(std::move(save)),
      fade_(script_.fade_in, script_.fade_out), help_(script_.pages.size()),
      repeat_x_(script_.repeat_delay, script_.repeat_interval),
      repeat_y_(script_.repeat_delay, script_.repeat_interval),
      repeat_page_(script_.repeat_delay, script_.repeat_interval),
      repeat_edit_(script_.repeat_delay, script_.repeat_interval),
      slide_x_(script_.animation.slide_from) {
  // The name so far, the cursor after it (0x2c8cb).
  name_ = split_characters(save_.player_name);
  if (std::cmp_greater(name_.size(), script_.max_length)) {
    name_.resize(static_cast<std::size_t>(script_.max_length));
  }
  tab_row_cursor_ = tab_row();
}

std::vector<std::string> NameEntry::take_sounds() {
  return std::exchange(sounds_, {});
}

void NameEntry::sound(const std::string &name) {
  if (!name.empty()) {
    sounds_.push_back(name);
  }
}

std::array<float, 2> NameEntry::name_box_scale() const noexcept {
  const NameEntryAnimation &look = script_.animation;
  const int step = std::min(look.grow_steps, static_cast<int>(grow_ * original_rate + 1e-6));
  if (step <= 0) {
    return {0.0F, 0.0F};
  }
  // Wider than full size first, then taller while it narrows (0xbe87c).
  const auto widen = static_cast<float>(look.grow_widen_steps);
  if (step < look.grow_widen_steps) {
    return {static_cast<float>(step) / widen * look.grow_overshoot, look.grow_start_height};
  }
  if (step < look.grow_steps) {
    const auto rest = static_cast<float>(look.grow_steps - look.grow_widen_steps);
    const auto into = static_cast<float>(step - look.grow_widen_steps) / rest;
    return {look.grow_overshoot - (look.grow_overshoot - 1.0F) * into,
            look.grow_start_height + (1.0F - look.grow_start_height) * into};
  }
  return {1.0F, 1.0F};
}

bool NameEntry::name_shown() const noexcept {
  return grow_ * original_rate + 1e-6 >= script_.animation.grow_steps;
}

float NameEntry::cursor_alpha() const noexcept {
  return static_cast<float>(std::sin(pulse_)) * script_.animation.pulse_alpha;
}

void NameEntry::update(const double tick_seconds, const InputFrame &input) {
  if (done()) {
    return;
  }
  seconds_ += tick_seconds;
  box_.update(tick_seconds);
  help_.advance(page_, tick_seconds);
  const bool ready = fade_.update(tick_seconds);
  if (!ready) {
    return;
  }
  if (box_.is_open()) {
    // The notice waits for confirm, then the name starts again (0x2d7ac).
    if (input.any_pressed(script_.confirm_buttons)) {
      sound(script_.select_sound);
      box_.close();
    }
    return;
  }
  animate(tick_seconds);
  controls(tick_seconds, input);
  keyboard(input);
}

void NameEntry::animate(const double tick_seconds) {
  const NameEntryAnimation &look = script_.animation;
  // 0x2ca50: the grid and tabs fade in, the controls box slides in halfway
  // and its hints fade in, and the name box grows once the box is close.
  if (intro_ + time_epsilon >= 1.0) {
    page_alpha_ = rise(page_alpha_, look.page_per_second, tick_seconds);
  }
  intro_ = rise(intro_, look.intro_per_second, tick_seconds);
  if (intro_ > look.slide_after + time_epsilon) {
    if (slide_x_ <= look.slide_to) {
      slide_x_ = look.slide_to;
      hints_ = rise(hints_, look.hints_per_second, tick_seconds);
    } else {
      slide_x_ -= look.slide_per_second * static_cast<float>(tick_seconds);
    }
  }
  if (slide_x_ < look.grow_below_x && !name_shown()) {
    grow_ += tick_seconds;
  }
  pulse_ += look.pulse_per_second * tick_seconds;
  if (pulse_ > std::numbers::pi) {
    pulse_ -= std::numbers::pi;
  }
}

void NameEntry::controls(const double tick_seconds, const InputFrame &input) {
  if (tabs_) {
    move_tabs(tick_seconds, input);
  } else {
    saved_row_ = row_;
    move_grid(tick_seconds, input);
  }
  page_buttons(tick_seconds, input);
  // Onto the tab row, or up off it; End has no grid, so up from End goes
  // back to the page before it (0x2cd21).
  const bool was_tabs = tabs_;
  if (!tabs_ && row_ == tab_row()) {
    tabs_ = true;
  } else if (tabs_ && tab_row_cursor_ != tab_row()) {
    tabs_ = false;
    if (page_ == end_page()) {
      page_ = previous_;
      row_ = saved_row_;
    } else {
      row_ = tab_row_cursor_;
    }
    tab_row_cursor_ = tab_row();
  }
  if (page_ != end_page() && row_ != tab_row()) {
    tabs_ = false;
  }
  if (input.any_pressed(script_.cancel_buttons) && page_ != end_page()) {
    sound(script_.page_sound);
    previous_ = page_;
    page_ = end_page();
    tabs_ = true;
    tab_row_cursor_ = tab_row();
    saved_row_ = was_tabs ? tab_row() - 1 : row_;
    row_ = tab_row();
  }
  bool fresh = false;
  const int edit =
      repeat_edit_.step(tick_seconds, input, script_.delete_buttons, script_.space_buttons, fresh);
  if (input.any_pressed(script_.confirm_buttons)) {
    sound(script_.select_sound);
    if (!tabs_) {
      const auto &cells = script_.pages[static_cast<std::size_t>(page_)].rows;
      type(cells[static_cast<std::size_t>(row_)][static_cast<std::size_t>(column_)]);
    } else if (page_ == end_page()) {
      finish();
    }
  } else if (edit < 0) {
    if (!name_.empty()) {
      name_.pop_back();
      sound(script_.delete_sound);
    }
  } else if (edit > 0) {
    sound(script_.select_sound);
    type(space);
  }
}

void NameEntry::move_grid(const double tick_seconds, const InputFrame &input) {
  // Columns wrap on a new press (flags 0x17, sys 4); rows stop at the ends
  // (0x12, sys 3), the row below the last being the tab row.
  bool fresh = false;
  int step = repeat_x_.step(tick_seconds, input, button::left, button::right, fresh);
  if (const int moved = move_cursor(column_, step, fresh, script_.columns); moved != column_) {
    column_ = moved;
    sound(script_.move_x_sound);
  }
  step = repeat_y_.step(tick_seconds, input, button::up, button::down, fresh);
  if (const int moved = std::clamp(row_ + step, 0, tab_row()); moved != row_) {
    row_ = moved;
    sound(script_.move_sound);
  }
}

void NameEntry::move_tabs(const double tick_seconds, const InputFrame &input) {
  // A tab is chosen as soon as the cursor lands on it.
  bool fresh = false;
  int step = repeat_x_.step(tick_seconds, input, button::left, button::right, fresh);
  const int pages = static_cast<int>(script_.pages.size());
  if (const int moved = move_cursor(page_, step, fresh, pages); moved != page_) {
    sound(script_.move_x_sound);
    change_page(moved);
  }
  step = repeat_y_.step(tick_seconds, input, button::up, button::down, fresh);
  if (const int moved = std::clamp(tab_row_cursor_ + step, 0, tab_row());
      moved != tab_row_cursor_) {
    tab_row_cursor_ = moved;
    sound(script_.move_sound);
  }
}

void NameEntry::page_buttons(const double tick_seconds, const InputFrame &input) {
  // The page buttons step through the pages, wrapping only on a new press;
  // reaching End puts the cursor on the tabs (0x2d194).
  bool fresh = false;
  const int step = repeat_page_.step(tick_seconds, input, script_.page_back_buttons,
                                     script_.page_forward_buttons, fresh);
  if (step > 0 && page_ < end_page()) {
    change_page(page_ + 1);
  } else if (step > 0 && fresh) {
    change_page(0);
  } else if (step < 0 && page_ > 0) {
    change_page(page_ - 1);
  } else if (step < 0 && fresh) {
    change_page(end_page());
  }
  if (page_ == end_page()) {
    tabs_ = true;
  }
}

void NameEntry::change_page(const int page) {
  sound(script_.page_sound);
  previous_ = page_;
  page_ = page;
  page_alpha_ = 0.0;
  help_.restart(page_);
}

void NameEntry::type(const std::string &character) {
  // Past the last place, typing replaces the last character.
  if (std::cmp_less(name_.size(), script_.max_length)) {
    name_.push_back(character);
  } else {
    name_.back() = character;
  }
}

void NameEntry::finish() {
  // A name of nothing but spaces is no name (0x2d723).
  if (std::ranges::all_of(name_, is_space)) {
    name_.clear();
    box_.open(MessageBox::Kind::notice, script_.no_name, 0);
    sound(box_.style().open_sound);
    return;
  }
  save_.player_name.clear();
  for (const auto &character : name_) {
    save_.player_name += character;
  }
  fade_.leave(script_.exit_to);
}

} // namespace phantomcrash
