#include "phantomcrash/vs_rules.hpp"

#include "phantomcrash/battle.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {
namespace {

// Robots that must still stand for the round to go on (0x046f81).
constexpr int standing_to_fight = 2;
// Armour below this is gone (robot +0x44 < 1).
constexpr float armour_left = 1.0F;

} // namespace

bool vs_standing(const Combatant &robot) {
  if (!robot.alive || robot.dying || robot.hp < armour_left) {
    return false;
  }
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    if (robot.weapons[slot] != nullptr && !robot.lost[slot]) {
      return true;
    }
  }
  return false;
}

VsMenuInput vs_menu_input(const InputFrame &input) {
  const auto pressed = [&input](const Buttons mask) { return input.any_pressed(mask); };
  return {pressed(button::up | button::pad_up),
          pressed(button::down | button::pad_down),
          pressed(button::left | button::pad_left),
          pressed(button::right | button::pad_right),
          pressed(button::confirm),
          pressed(button::cancel)};
}

VsRound::VsRound(VsRoundTuning tuning, const int robots)
    : tuning_(tuning), standing_(static_cast<std::size_t>(std::max(robots, 0)), true) {}

void VsRound::enter(const VsRoundStage stage) {
  stage_ = stage;
  stage_seconds_ = 0.0;
}

void VsRound::update(const std::vector<bool> &standing, const VsMenuInput &input,
                     const double seconds) {
  stage_seconds_ += seconds;
  for (std::size_t index = 0; index < standing_.size() && index < standing.size(); ++index) {
    standing_[index] = standing_[index] && standing[index];
  }
  switch (stage_) {
  case VsRoundStage::fight:
    if (std::ranges::count(standing_, true) < standing_to_fight) {
      enter(VsRoundStage::settle);
    }
    break;
  case VsRoundStage::settle:
    if (stage_seconds_ * tuning_.frame_rate >= tuning_.settle_frames) {
      cursor_ = 0;
      enter(VsRoundStage::menu);
    }
    break;
  case VsRoundStage::menu:
    update_menu(input);
    break;
  case VsRoundStage::confirm_exit:
    update_question(input);
    break;
  case VsRoundStage::fade:
    if (stage_seconds_ * tuning_.frame_rate >= tuning_.fade_frames) {
      enter(VsRoundStage::done);
    }
    break;
  case VsRoundStage::done:
    break;
  }
}

void VsRound::update_menu(const VsMenuInput &input) {
  if (input.up || input.down) {
    cursor_ = (cursor_ + (input.down ? 1 : vs_menu_items - 1)) % vs_menu_items;
    sounds_.push_back(tuning_.move_sound);
  } else if (input.confirm) {
    sounds_.push_back(tuning_.confirm_sound);
    if (cursor_ == vs_menu_exit) {
      answer_ = vs_answer_no;
      enter(VsRoundStage::confirm_exit);
    } else {
      enter(VsRoundStage::fade);
    }
  } else if (input.cancel && cursor_ != vs_menu_exit) {
    cursor_ = vs_menu_exit;
    sounds_.push_back(tuning_.cancel_sound);
  }
}

void VsRound::update_question(const VsMenuInput &input) {
  if (input.left || input.right) {
    answer_ = answer_ == vs_answer_yes ? vs_answer_no : vs_answer_yes;
    sounds_.push_back(tuning_.answer_sound);
  } else if (input.confirm) {
    sounds_.push_back(tuning_.confirm_sound);
    enter(answer_ == vs_answer_yes ? VsRoundStage::fade : VsRoundStage::menu);
  } else if (input.cancel) {
    if (answer_ != vs_answer_no) {
      answer_ = vs_answer_no;
      sounds_.push_back(tuning_.cancel_sound);
    } else {
      enter(VsRoundStage::menu);
    }
  }
}

bool VsRound::draw() const noexcept {
  return std::ranges::none_of(standing_, [](const bool standing) { return standing; });
}

VsOutcome VsRound::outcome(const int robot) const {
  if (stage_ == VsRoundStage::fight || robot < 0 || robot >= static_cast<int>(standing_.size())) {
    return VsOutcome::none;
  }
  if (draw()) {
    return VsOutcome::draw;
  }
  return standing_[static_cast<std::size_t>(robot)] ? VsOutcome::win : VsOutcome::lose;
}

double VsRound::fade() const noexcept {
  if (stage_ == VsRoundStage::done) {
    return 1.0;
  }
  if (stage_ != VsRoundStage::fade || tuning_.fade_frames <= 0.0) {
    return 0.0;
  }
  return std::min(stage_seconds_ * tuning_.frame_rate / tuning_.fade_frames, 1.0);
}

std::optional<VsChoice> VsRound::choice() const {
  if (stage_ != VsRoundStage::done) {
    return std::nullopt;
  }
  return static_cast<VsChoice>(cursor_);
}

std::vector<std::string> VsRound::take_sounds() {
  return std::exchange(sounds_, {});
}

void VsRound::tally(const std::vector<int> &ports, std::array<int, controller_ports> &wins,
                    std::array<int, controller_ports> &losses) {
  if (tallied_ || stage_ != VsRoundStage::done || draw()) {
    return;
  }
  tallied_ = true;
  for (std::size_t robot = 0; robot < standing_.size() && robot < ports.size(); ++robot) {
    const int port = ports[robot];
    if (port < 0 || port >= controller_ports) {
      continue;
    }
    ++(standing_[robot] ? wins : losses)[static_cast<std::size_t>(port)];
  }
}

} // namespace phantomcrash
