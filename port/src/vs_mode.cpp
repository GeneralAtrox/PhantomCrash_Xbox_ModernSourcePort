// VS MODE: the players' panels, the phases and their timing; the players'
// input is in vs_mode_input.cpp and the area select in vs_mode_area.cpp.

#include "phantomcrash/vs_mode.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {
void VsReveal::open(const double delay) {
  if (!open_) {
    open_ = true;
    delay_ = delay;
  }
}

void VsReveal::close(const double delay) {
  if (open_) {
    open_ = false;
    delay_ = delay;
  }
}

void VsReveal::set(const bool shown) noexcept {
  open_ = shown;
  amount_ = shown ? 1.0 : 0.0;
  delay_ = 0.0;
}

void VsReveal::update(double tick_seconds) {
  if (delay_ > 0.0) {
    delay_ -= tick_seconds;
    if (delay_ > 0.0) {
      return;
    }
    tick_seconds = -delay_;
    delay_ = 0.0;
  }
  const double step = seconds_ > 0.0 ? tick_seconds / seconds_ : 1.0;
  amount_ = std::clamp(amount_ + (open_ ? step : -step), 0.0, 1.0);
}

VsMode::VsMode(VsModeScript script, const PartTables &parts, const GarageTables &tables,
               const std::vector<VsScoobee> &kept, VsSession &session, const VsModeEntry entry)
    : script_(std::move(script)), parts_(parts), tables_(tables), kept_(kept), session_(session),
      fade_(script_.fade_in, script_.fade_out),
      across_(script_.repeat_delay, script_.repeat_interval), popup_(script_.timings.question_grow),
      zone_(script_.area.time_zone.grow_seconds) {
  const VsTimings &timings = script_.timings;
  const VsCards &cards = script_.cards;
  for (VsPlayer &player : players_) {
    player.panel = VsReveal(timings.panel_grow + timings.label_seconds);
    player.content = VsReveal(timings.open_seconds);
    player.extra = VsReveal(timings.open_seconds);
    player.overlay = VsReveal(timings.open_seconds);
    player.cards = CardRow({cards.visible, cards.scroll_seconds, cards.push});
    player.repeat = CursorRepeat(script_.repeat_delay, script_.repeat_interval);
  }
  connected_[0] = true;
  const auto joined = std::ranges::count(session_.joined, true);
  if (entry.from_battle && joined >= players_to_start) {
    for (std::size_t port = 0; port < players_.size(); ++port) {
      VsPlayer &player = players_[port];
      player.joined = session_.joined[port];
      player.decided = player.joined;
      const int chosen = std::clamp(session_.chosen[port], 0, count() - 1);
      player.first = std::clamp(chosen, 0, std::max(count() - cards.visible, 0));
      player.row = chosen - player.first;
    }
    if (session_.resume == VsPhase::select) {
      // The battle's SELECT SV (0x03f4cf 1): the same players choose again.
      session_.resume = VsPhase::area;
      start_phase(VsPhase::select);
      return;
    }
    enter_area();
    area_cursor_ = std::clamp(session_.area_cursor, 0, static_cast<int>(items_.size()) - 1);
    picture_ = wanted_picture();
    return;
  }
  session_ = VsSession{};
  start_phase(VsPhase::entry);
}

int VsMode::joined_count() const {
  return static_cast<int>(std::ranges::count(players_, true, &VsPlayer::joined));
}

int VsMode::count() const {
  return static_cast<int>(script_.cards.presets.size() + kept_.size());
}

int VsMode::preset(const int index) const {
  return index >= 0 && index < static_cast<int>(script_.cards.presets.size()) ? index : -1;
}

const VsScoobee *VsMode::kept(const int index) const {
  const int at = index - static_cast<int>(script_.cards.presets.size());
  if (at < 0 || at >= static_cast<int>(kept_.size())) {
    return nullptr;
  }
  return &kept_[static_cast<std::size_t>(at)];
}

bool VsMode::usable(const int index) const {
  if (const int shown = preset(index); shown >= 0) {
    return tables_.robot(script_.cards.presets[static_cast<std::size_t>(shown)].robot) != nullptr;
  }
  const VsScoobee *scoobee = kept(index);
  return scoobee != nullptr && !vs_scoobee_damaged(*scoobee, parts_);
}

int VsMode::maker(const int index) const {
  const VsScoobee *scoobee = kept(index);
  return scoobee != nullptr ? vs_scoobee_maker(*scoobee, parts_) : -1;
}

std::string VsMode::name(const int index) const {
  if (const auto record = robot(index)) {
    return kept(index) != nullptr ? kept(index)->name : record->name;
  }
  return {};
}

std::optional<RobotRecord> VsMode::robot(const int index) const {
  if (const int shown = preset(index); shown >= 0) {
    const RobotRecord *record =
        tables_.robot(script_.cards.presets[static_cast<std::size_t>(shown)].robot);
    return record != nullptr ? std::optional<RobotRecord>(*record) : std::nullopt;
  }
  const VsScoobee *scoobee = kept(index);
  return scoobee != nullptr ? std::optional<RobotRecord>(scoobee->robot) : std::nullopt;
}

const std::string &VsMode::help_text() const {
  static const std::string none;
  const char *names[] = {"entry", "select", "area"};
  const auto found = script_.helps.find(names[static_cast<int>(phase_)]);
  return found != script_.helps.end() ? found->second : none;
}

float VsMode::black_alpha() const noexcept {
  float black = 0.0F;
  if (black_seconds_ >= 0.0) {
    const double fade = std::max(script_.timings.fade_seconds, 1e-6);
    const double along =
        black_seconds_ < fade ? black_seconds_ / fade : 2.0 - black_seconds_ / fade;
    black = static_cast<float>(std::clamp(along, 0.0, 1.0));
  }
  return std::max(fade_.black_alpha(), black);
}

// 0x04183a and 0x042312: the phase's message opens, then the panels one
// after another; the entry lets everyone go, the Scoobee select starts
// every strip at its first card.
void VsMode::start_phase(const VsPhase phase) {
  phase_ = phase;
  step_ = VsStep::intro;
  phase_seconds_ = 0.0;
  help_seconds_ = 0.0;
  message_.clear();
  popup_.set(false);
  for (VsPlayer &player : players_) {
    player.started = false;
    player.wants_content = false;
    player.content.set(false);
    player.extra.set(false);
    player.overlay.set(false);
    player.repeat.reset();
    player.decided = false;
    player.first = 0;
    player.row = 0;
    if (phase == VsPhase::entry) {
      player.joined = false;
    }
  }
  if (phase == VsPhase::entry) {
    session_.wins.fill(0);
    session_.losses.fill(0);
  }
}

// 0x03ffc9: what an open panel shows; the entry's for a port with a
// controller, the Scoobee select's for a player who entered.
void VsMode::open_content(const int port) {
  VsPlayer &player = players_[static_cast<std::size_t>(port)];
  const bool on =
      phase_ == VsPhase::entry ? connected_[static_cast<std::size_t>(port)] : player.joined;
  (on ? player.content : player.overlay).open();
}

void VsMode::update_players(const double tick_seconds) {
  const VsIntro &intro = script_.intro;
  for (int port = 0; port < controller_ports; ++port) {
    VsPlayer &player = players_[static_cast<std::size_t>(port)];
    if (phase_ != VsPhase::area && step_ == VsStep::intro && !player.started &&
        phase_seconds_ >= intro.shrink_from + intro.stagger * port) {
      player.started = true;
      if (player.panel.shown()) {
        open_content(port);
      } else {
        player.panel.open();
        player.wants_content = true;
      }
    }
    player.panel.update(tick_seconds);
    player.content.update(tick_seconds);
    player.extra.update(tick_seconds);
    player.overlay.update(tick_seconds);
    player.cards.update(tick_seconds);
    if (player.wants_content && player.panel.shown()) {
      player.wants_content = false;
      open_content(port);
    }
  }
}

// 0x03fb73 (and 0x03fa5f with the panels): each player's contents close
// a stagger after the one before, the panels after all of them.
void VsMode::close_contents(const bool panels_too) {
  const double stagger = script_.intro.stagger;
  for (int port = 0; port < controller_ports; ++port) {
    VsPlayer &player = players_[static_cast<std::size_t>(port)];
    player.wants_content = false;
    player.content.close(stagger * port);
    player.extra.close(stagger * port);
    player.overlay.close(stagger * port);
    if (panels_too) {
      player.panel.close(stagger * (port + controller_ports));
    }
  }
}

void VsMode::update(const double tick_seconds, const InputFrame &input) {
  seconds_ += tick_seconds;
  help_seconds_ += tick_seconds;
  phase_seconds_ += tick_seconds;
  step_seconds_ += tick_seconds;
  for (std::size_t port = 0; port < connected_.size(); ++port) {
    connected_[port] = input.ports[port].connected;
  }
  update_players(tick_seconds);
  popup_.update(tick_seconds);
  const bool takes_input = fade_.update(tick_seconds);
  step_timers(tick_seconds);
  if (phase_ == VsPhase::area) {
    update_area(tick_seconds);
  }
  if (!takes_input || fade_.leaving()) {
    return;
  }
  switch (step_) {
  case VsStep::input:
    if (phase_ == VsPhase::entry) {
      entry_input(tick_seconds, input);
    } else if (phase_ == VsPhase::select) {
      select_input(tick_seconds, input);
    } else {
      area_input(tick_seconds, input);
    }
    break;
  case VsStep::question:
    question_input(tick_seconds, input);
    break;
  case VsStep::abandon:
    abandon_input(tick_seconds, input);
    break;
  case VsStep::zone:
    zone_input(tick_seconds, input);
    break;
  default:
    break;
  }
}

void VsMode::step_timers(const double tick_seconds) {
  const VsIntro &intro = script_.intro;
  const VsTimings &timings = script_.timings;
  if (step_ == VsStep::intro && phase_ != VsPhase::area &&
      phase_seconds_ >=
          intro.shrink_from + intro.stagger * (controller_ports - 1) + intro.settle_seconds) {
    step_ = VsStep::input;
  } else if (step_ == VsStep::answered_no && popup_.hidden()) {
    // NO: the entry lets everyone go, the Scoobee select every choice.
    for (VsPlayer &player : players_) {
      if (phase_ == VsPhase::entry) {
        player.joined = false;
      }
      player.decided = false;
      player.extra.close();
    }
    step_ = VsStep::input;
  } else if (step_ == VsStep::leaving && step_seconds_ >= timings.leave_seconds) {
    start_phase(after_);
  } else if (step_ == VsStep::exiting && step_seconds_ >= timings.exit_seconds &&
             !fade_.leaving()) {
    fade_.leave(script_.exit_to);
  }
  if (black_seconds_ >= 0.0) {
    const double before = black_seconds_;
    black_seconds_ += tick_seconds;
    if (before < timings.fade_seconds && black_seconds_ >= timings.fade_seconds) {
      if (after_ == VsPhase::area) {
        enter_area();
      } else {
        start_phase(after_);
      }
    }
    if (black_seconds_ >= 2.0 * timings.fade_seconds) {
      black_seconds_ = -1.0;
    }
  }
}

} // namespace phantomcrash
