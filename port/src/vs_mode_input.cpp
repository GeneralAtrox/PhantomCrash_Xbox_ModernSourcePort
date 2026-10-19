// VS MODE's input through the entry and the Scoobee select, and the
// questions they ask.

#include "phantomcrash/vs_mode.hpp"

#include <algorithm>

namespace phantomcrash {

// 0x040aec: each port enters, asks to start or leaves; a port without a
// controller shows No Controller and lets its player go.
void VsMode::entry_input(const double /*tick_seconds*/, const InputFrame &input) {
  bool start = false;
  bool leave = false;
  for (int port = 0; port < controller_ports; ++port) {
    VsPlayer &player = players_[static_cast<std::size_t>(port)];
    if (!connected_[static_cast<std::size_t>(port)]) {
      player.joined = false;
      player.content.close();
      player.extra.close();
      if (player.panel.shown()) {
        player.overlay.open();
      }
      continue;
    }
    player.overlay.close();
    if (player.panel.shown()) {
      player.content.open();
    }
    const InputFrame own = port_frame(input, port);
    if (own.any_pressed(script_.confirm_buttons)) {
      if (!player.joined) {
        player.joined = true;
        sounds_.play(script_.select_sound);
      } else {
        start = true;
      }
    } else if (own.any_pressed(script_.cancel_buttons)) {
      if (player.joined) {
        player.joined = false;
        sounds_.play(script_.no_sound);
      } else {
        leave = true;
      }
    }
  }
  const bool enough = joined_count() >= players_to_start;
  for (VsPlayer &player : players_) {
    if (player.joined && enough) {
      player.extra.open();
    } else {
      player.extra.close();
    }
  }
  if (start) {
    sounds_.play(enough ? script_.select_sound : script_.no_sound);
    if (enough) {
      ask();
    }
  } else if (leave) {
    sounds_.play(script_.no_sound);
    step_ = VsStep::exiting;
    step_seconds_ = 0.0;
    close_contents(true);
  }
}

// 0x0418c6: each player moves along their strip and picks a Scoobee, or
// puts it back; once all have picked, Are you ready?
void VsMode::select_input(const double tick_seconds, const InputFrame &input) {
  bool back = false;
  for (int port = 0; port < controller_ports; ++port) {
    VsPlayer &player = players_[static_cast<std::size_t>(port)];
    if (!player.joined) {
      continue;
    }
    const InputFrame own = port_frame(input, port);
    if (player.decided) {
      if (own.any_pressed(script_.cancel_buttons)) {
        player.decided = false;
        player.extra.close();
        sounds_.play(script_.no_sound);
      } else if (own.any_pressed(script_.confirm_buttons)) {
        sounds_.play(script_.no_sound);
      }
      continue;
    }
    bool fresh = false;
    const int step = player.repeat.step(tick_seconds, own, script_.left, script_.right, fresh);
    if (step != 0 && !player.cards.scrolling()) {
      if (player.cards.move(player.first, player.row, step, count())) {
        sounds_.play(script_.move_sound);
      }
    } else if (own.any_pressed(script_.confirm_buttons) && !player.cards.scrolling()) {
      if (usable(player.chosen())) {
        player.decided = true;
        player.extra.open();
        sounds_.play(script_.select_sound);
      } else {
        sounds_.play(script_.unusable_sound);
      }
    } else if (own.any_pressed(script_.cancel_buttons)) {
      back = true;
    }
  }
  if (back) {
    message_ = "abandon";
    choice_ = no_choice;
    step_ = VsStep::abandon;
    across_.reset();
    return;
  }
  const bool all = joined_count() > 0 && std::ranges::all_of(players_, [](const VsPlayer &player) {
                     return !player.joined || (player.decided && player.extra.shown());
                   });
  if (all) {
    ask();
  }
}

void VsMode::ask() {
  popup_.open();
  choice_ = yes_choice;
  step_ = VsStep::question;
  across_.reset();
}

// Are you ready?: left/right choose (sys 4), cancel moves to NO (sys 4).
void VsMode::question_input(const double tick_seconds, const InputFrame &input) {
  if (!popup_.shown()) {
    return;
  }
  bool fresh = false;
  if (const int step = across_.step(tick_seconds, input, script_.left, script_.right, fresh)) {
    const int choice = std::clamp(choice_ + step, yes_choice, no_choice);
    if (choice != choice_) {
      choice_ = choice;
      sounds_.play(script_.choose_sound);
    }
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    sounds_.play(script_.select_sound);
    answer(choice_ == yes_choice);
  } else if (input.any_pressed(script_.cancel_buttons) && choice_ != no_choice) {
    choice_ = no_choice;
    sounds_.play(script_.to_no_sound);
  }
}

void VsMode::answer(const bool yes) {
  popup_.close();
  if (!yes) {
    step_ = VsStep::answered_no;
  } else if (phase_ == VsPhase::entry) {
    step_ = VsStep::leaving;
    step_seconds_ = 0.0;
    after_ = VsPhase::select;
    close_contents(false);
  } else {
    step_ = VsStep::blackout;
    after_ = VsPhase::area;
    black_seconds_ = 0.0;
  }
}

// Giving up the play data (0x2036e8): YES returns to the entry.
void VsMode::abandon_input(const double tick_seconds, const InputFrame &input) {
  bool fresh = false;
  if (const int step = across_.step(tick_seconds, input, script_.left, script_.right, fresh)) {
    const int choice = std::clamp(choice_ + step, yes_choice, no_choice);
    if (choice != choice_) {
      choice_ = choice;
      sounds_.play(script_.choose_sound);
    }
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    sounds_.play(script_.select_sound);
    message_.clear();
    if (choice_ == yes_choice) {
      step_ = VsStep::leaving;
      step_seconds_ = 0.0;
      after_ = VsPhase::entry;
      close_contents(false);
    } else {
      step_ = VsStep::input;
    }
  } else if (input.any_pressed(script_.cancel_buttons) && choice_ != no_choice) {
    choice_ = no_choice;
    sounds_.play(script_.to_no_sound);
  }
}

} // namespace phantomcrash
