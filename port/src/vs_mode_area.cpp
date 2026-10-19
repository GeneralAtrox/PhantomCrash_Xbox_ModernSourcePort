// VS MODE's area select (0x041778, 0x0411ac): the area, RANDOM or EXIT, the
// time of day, and the battle.

#include "phantomcrash/vs_mode.hpp"

#include <algorithm>

namespace phantomcrash {
namespace {

constexpr double frame_seconds = 1.0 / 60.0;
constexpr int day_choice = 0;
constexpr int night_choice = 1;

} // namespace

bool VsMode::crowd() const {
  return joined_count() >= script_.area.crowd.players;
}

InputFrame VsMode::joined_input(const InputFrame &input) const {
  InputFrame joined;
  joined.controllers = 1;
  for (int port = 0; port < controller_ports; ++port) {
    if (players_[static_cast<std::size_t>(port)].joined) {
      const PortInput &own = input.ports[static_cast<std::size_t>(port)];
      joined.held |= own.held;
      joined.pressed |= own.pressed;
    }
  }
  return joined;
}

// 0x041778: the players' choices are kept and the items slide in; with a
// crowd only the arena's items are there, the cursor on the arena.
void VsMode::enter_area() {
  phase_ = VsPhase::area;
  step_ = VsStep::input;
  phase_seconds_ = 0.0;
  help_seconds_ = 0.0;
  message_.clear();
  popup_.set(false);
  zone_.set(false);
  for (std::size_t port = 0; port < players_.size(); ++port) {
    VsPlayer &player = players_[port];
    session_.joined[port] = player.joined;
    session_.chosen[port] = player.chosen();
    player.panel.set(false);
    player.content.set(false);
    player.extra.set(false);
    player.overlay.set(false);
  }
  items_.clear();
  const int first = crowd() ? script_.area.crowd.first : 0;
  for (int item = first; item < static_cast<int>(script_.area.items.size()); ++item) {
    items_.push_back(item);
  }
  area_cursor_ = 0;
  area_seconds_ = 0.0;
  previous_ = {};
  picture_ = wanted_picture();
  picture_seconds_ = 0.0;
  random_seconds_ = 0.0;
  random_stage_ = crowd() ? script_.area.crowd.stages.front() : 0;
  across_.reset();
}

VsMode::Picture VsMode::wanted_picture() const {
  if (items_.empty()) {
    return {};
  }
  const bool night = step_ == VsStep::zone && zone_.opening() && zone_choice_ == night_choice;
  return {items_[static_cast<std::size_t>(area_cursor_)], night};
}

float VsMode::slide_along() const {
  const double length = std::max(script_.area.slide_seconds, 1e-6);
  return static_cast<float>(std::clamp(area_seconds_ / length, 0.0, 1.0));
}

float VsMode::picture_along() const {
  const double length = std::max(script_.area.fade_seconds, 1e-6);
  return static_cast<float>(std::clamp(picture_seconds_ / length, 0.0, 1.0));
}

// The pictures cross-fade to the cursor's once the fade before has ended;
// RANDOM's picture changes every random_every while it shows.
void VsMode::update_area(const double tick_seconds) {
  const VsAreaSelect &area = script_.area;
  const double before = area_seconds_;
  area_seconds_ += tick_seconds;
  if (before < frame_seconds && area_seconds_ >= frame_seconds) {
    sounds_.play(script_.slide_sound);
  }
  picture_seconds_ += tick_seconds;
  zone_.update(tick_seconds);
  const bool faded = picture_seconds_ >= area.fade_seconds;
  if (const Picture wanted = wanted_picture(); faded && !(wanted == picture_)) {
    previous_ = picture_;
    picture_ = wanted;
    picture_seconds_ = 0.0;
    return;
  }
  const bool random =
      picture_.item >= 0 &&
      area.items[static_cast<std::size_t>(picture_.item)].kind == VsAreaKind::random;
  if (!random || !faded) {
    return;
  }
  random_seconds_ += tick_seconds;
  if (random_seconds_ < area.random_every) {
    return;
  }
  random_seconds_ = 0.0;
  if (crowd()) {
    const std::vector<int> &stages = area.crowd.stages;
    const auto at = std::ranges::find(stages, random_stage_);
    random_stage_ = at == stages.end() || at + 1 == stages.end() ? stages.front() : *(at + 1);
  } else if (!area.random_pictures.empty()) {
    const auto count = static_cast<unsigned>(area.random_pictures.size());
    random_stage_ = static_cast<int>(random_() % count);
  }
}

// 0x0411ac: any entered player moves the cursor (sys 4, no wrapping);
// cancel moves to EXIT (sys 4); confirm once the pictures have faded.
void VsMode::area_input(const double tick_seconds, const InputFrame &input) {
  if (black_seconds_ >= 0.0 || items_.empty()) {
    return;
  }
  const InputFrame own = joined_input(input);
  bool fresh = false;
  if (const int step = across_.step(tick_seconds, own, script_.left, script_.right, fresh)) {
    const int cursor = std::clamp(area_cursor_ + step, 0, static_cast<int>(items_.size()) - 1);
    if (cursor != area_cursor_) {
      area_cursor_ = cursor;
      sounds_.play(script_.choose_sound);
    }
    return;
  }
  if (own.any_pressed(script_.cancel_buttons)) {
    const int exit = static_cast<int>(items_.size()) - 1;
    if (area_cursor_ != exit) {
      area_cursor_ = exit;
      sounds_.play(script_.to_no_sound);
    }
    return;
  }
  const bool settled =
      slide_along() >= 1.0F && picture_along() >= 1.0F && picture_ == wanted_picture();
  if (own.any_pressed(script_.confirm_buttons) && settled) {
    confirm_area();
  }
}

void VsMode::confirm_area() {
  sounds_.play(script_.select_sound);
  const VsAreaSelect &area = script_.area;
  const VsAreaItem &item = area.items[static_cast<std::size_t>(items_[area_cursor_])];
  switch (item.kind) {
  case VsAreaKind::exit:
    step_ = VsStep::blackout;
    after_ = VsPhase::select;
    black_seconds_ = 0.0;
    break;
  case VsAreaKind::random:
    if (crowd()) {
      const std::vector<int> &stages = area.crowd.stages;
      start_battle(stages[random_() % stages.size()]);
    } else {
      const auto count =
          static_cast<unsigned>(std::max<std::size_t>(area.random_pictures.size(), 1));
      start_battle(static_cast<int>(random_() % count));
    }
    break;
  case VsAreaKind::area:
    if (!item.text.empty() && item.stages[0] == item.stages[1]) {
      start_battle(item.stages[0]);
      break;
    }
    zone_.open();
    zone_choice_ = day_choice;
    step_ = VsStep::zone;
    across_.reset();
    break;
  }
}

// The time-zone window (0x1ffb1c): DAY or NIGHT (sys 4), confirm starts
// the battle there, cancel closes it (sys 8).
void VsMode::zone_input(const double tick_seconds, const InputFrame &input) {
  if (!zone_.shown()) {
    return;
  }
  const InputFrame own = joined_input(input);
  bool fresh = false;
  if (const int step = across_.step(tick_seconds, own, script_.left, script_.right, fresh)) {
    const int choice = std::clamp(zone_choice_ + step, day_choice, night_choice);
    if (choice != zone_choice_) {
      zone_choice_ = choice;
      sounds_.play(script_.choose_sound);
    }
    return;
  }
  if (own.any_pressed(script_.confirm_buttons)) {
    sounds_.play(script_.select_sound);
    const VsAreaItem &item = script_.area.items[static_cast<std::size_t>(items_[area_cursor_])];
    start_battle(item.stages[static_cast<std::size_t>(zone_choice_)]);
  } else if (own.any_pressed(script_.cancel_buttons)) {
    sounds_.play(script_.no_sound);
    zone_.close();
    step_ = VsStep::input;
  }
}

// The battle on `stage` (the original's 0x046674: area * 2 + night) with
// each entered player's Scoobee.
void VsMode::start_battle(const int stage) {
  session_.stage = stage;
  session_.area_cursor = area_cursor_;
  for (std::size_t port = 0; port < players_.size(); ++port) {
    const VsPlayer &player = players_[port];
    session_.joined[port] = player.joined;
    session_.chosen[port] = player.chosen();
    session_.robots[port] = player.joined ? robot(player.chosen()) : std::nullopt;
  }
  fade_.leave(script_.battle_route);
}

} // namespace phantomcrash
