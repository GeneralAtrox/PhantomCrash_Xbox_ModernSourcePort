#include "phantomcrash/front_end.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {
namespace {

// Ticks of 1/60 s do not sum exactly (30 of them fall just short of 0.5).
constexpr double time_epsilon = 1e-9;

} // namespace

// Behaviour follows the original's title (0x3e82e) and menu (0x2c1ce,
// 0x2c2d9) tasks: START on the title opens the menu without a fade; the menu
// moves its cursor first, then checks confirm, then cancel or the idle timer.
FrontEnd::FrontEnd(TitleScript title, MainMenuScript menu)
    : title_(std::move(title)), menu_(std::move(menu)),
      cursor_repeat_(menu_.repeat_delay, menu_.repeat_interval) {
  enter(Mode::title, title_.fade_in);
}

void FrontEnd::open_menu_from(const std::string &screen) {
  enter(Mode::menu, menu_.fade_in);
  // The cursor returns to the item that led away (the original picks it from
  // the scene it returns from).
  const auto &items = menu_.main.items;
  for (std::size_t index = 0; index < items.size(); ++index) {
    const auto &item = items[index];
    bool led_there = item.go_to == screen;
    if (!item.submenu.empty()) {
      for (const auto &sub : menu_.submenus.at(item.submenu).items) {
        led_there = led_there || sub.go_to == screen;
      }
    }
    if (led_there) {
      main_cursor_ = static_cast<int>(index);
      break;
    }
  }
}

void FrontEnd::enter(const Mode mode, const double fade_in) {
  mode_ = mode;
  phase_ = fade_in > 0.0 ? Phase::fade_in : Phase::active;
  fade_seconds_ = fade_in;
  phase_elapsed_ = 0.0;
  submenu_.clear();
  if (mode == Mode::title) {
    idle_ = title_.idle_seconds;
  } else {
    idle_ = menu_.idle_seconds;
    main_cursor_ = menu_.main.default_cursor;
  }
}

std::vector<std::string> FrontEnd::take_sounds() {
  return std::exchange(sounds_, {});
}

void FrontEnd::sound(const std::string &name) {
  if (!name.empty()) {
    sounds_.push_back(name);
  }
}

const Menu *FrontEnd::menu() const noexcept {
  if (mode_ == Mode::title) {
    return nullptr;
  }
  return submenu_.empty() ? &menu_.main : &menu_.submenus.at(submenu_);
}

int FrontEnd::cursor() const noexcept {
  return submenu_.empty() ? main_cursor_ : sub_cursor_;
}

float FrontEnd::black_alpha() const noexcept {
  if (phase_ == Phase::active || fade_seconds_ <= 0.0) {
    return phase_ == Phase::fade_out ? 1.0F : 0.0F;
  }
  const auto t = static_cast<float>(std::clamp(phase_elapsed_ / fade_seconds_, 0.0, 1.0));
  return phase_ == Phase::fade_in ? 1.0F - t : t;
}

void FrontEnd::leave(const std::string &target, const double fade_out) {
  if (fade_out <= 0.0) {
    next_ = target;
    return;
  }
  pending_ = target;
  phase_ = Phase::fade_out;
  fade_seconds_ = fade_out;
  phase_elapsed_ = 0.0;
}

void FrontEnd::update(const double tick_seconds, const InputFrame &input,
                      const std::optional<int> pointed) {
  if (done()) {
    return;
  }
  seconds_ += tick_seconds;
  phase_elapsed_ += tick_seconds;
  controllers_ = input.controllers;
  switch (phase_) {
  case Phase::fade_in:
    // Input waits for the fade, as the original's fade-in tasks do.
    if (phase_elapsed_ + time_epsilon >= fade_seconds_) {
      phase_ = Phase::active;
    }
    return;
  case Phase::fade_out:
    if (phase_elapsed_ + time_epsilon >= fade_seconds_) {
      next_ = pending_;
    }
    return;
  case Phase::active:
    break;
  }
  if (mode_ == Mode::title) {
    update_title(input);
  } else {
    update_menu(tick_seconds, input, pointed);
  }
  idle_ -= tick_seconds;
}

void FrontEnd::update_title(const InputFrame &input) {
  if (input.any_pressed(title_.start_buttons) || input.clicked) {
    sound(title_.start_sound);
    enter(Mode::menu, 0.0);
    cursor_repeat_.reset();
    return;
  }
  if (idle_ <= time_epsilon) {
    leave(title_.idle_goto, title_.idle_fade_out);
  }
}

void FrontEnd::update_menu(const double tick_seconds, const InputFrame &input,
                           std::optional<int> pointed) {
  const Menu &level = *menu();
  int &cursor = submenu_.empty() ? main_cursor_ : sub_cursor_;
  const int count = static_cast<int>(level.items.size());
  if (pointed && (*pointed < 0 || *pointed >= count)) {
    pointed.reset();
  }
  bool fresh = false;
  const int step = cursor_repeat_.step(tick_seconds, input, button::up, button::down, fresh);
  int moved = move_cursor(cursor, step, fresh, count);
  if (pointed && (input.pointer_moved || input.clicked)) {
    moved = *pointed;
  }
  if (moved != cursor) {
    cursor = moved;
    sound(menu_.move_sound);
  }
  if (menu_.idle_resets_on_input && (input.pressed != 0 || input.clicked || input.pointer_moved)) {
    idle_ = menu_.idle_seconds;
  }

  if (input.any_pressed(menu_.confirm_buttons) || (input.clicked && pointed)) {
    const MenuItem &item = level.items[static_cast<std::size_t>(cursor)];
    if (!available(item)) {
      sound(menu_.unavailable_sound);
    } else if (!item.submenu.empty()) {
      sound(menu_.select_sound);
      submenu_ = item.submenu;
      sub_cursor_ = menu_.submenus.at(submenu_).default_cursor;
      idle_ = menu_.idle_seconds;
    } else {
      sound(menu_.select_sound);
      leave(item.go_to, menu_.fade_out);
    }
    return;
  }
  const bool cancel = input.any_pressed(menu_.cancel_buttons);
  if (cancel) {
    sound(menu_.cancel_sound);
  }
  if (!submenu_.empty()) {
    // The submenu has no timeout of its own; the timer keeps running.
    if (cancel) {
      submenu_.clear();
    }
    return;
  }
  if (cancel || idle_ <= time_epsilon) {
    // Back to the title without a fade; the music carries on.
    enter(Mode::title, 0.0);
  }
}

} // namespace phantomcrash
