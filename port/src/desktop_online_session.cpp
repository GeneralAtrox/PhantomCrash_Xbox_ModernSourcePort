#include "desktop_online_session.hpp"
#include "desktop_online_presentation.hpp"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace phantomcrash::online {

MatchSession::MatchSession(Room &room, CoopMatch &match, const std::uint64_t timeline,
                           const std::uint8_t players, MatchAssets *assets,
                           const std::uint32_t first_tick, const std::uint32_t input_delay)
    : room_(room), match_(match), players_(players),
      simulation_(match, players, first_tick, assets),
      prediction_(simulation_, players, first_tick), inputs_(room, prediction_, timeline, players),
      states_(room, timeline, first_tick),
      input_delay_(std::min(input_delay, max_input_delay_ticks)) {}

bool MatchSession::receive(const RoomMessage &message, const double now) {
  try {
    const bool used = inputs_.receive(message, now) || states_.receive(message);
    forget_checked_states();
    return used;
  } catch (...) {
    log_divergence();
    throw;
  }
}

void MatchSession::check_state(const double now) {
  // A state check is put off from a frame that already replayed a rollback,
  // for a few frames at most, so the two costs do not land in one frame.
  const auto rollbacks = prediction_.rollback_count();
  if (states_.due(std::min(next_tick(), confirmed_tick())) && rollbacks != checked_rollbacks_ &&
      deferred_checks_ < max_deferred_state_checks) {
    checked_rollbacks_ = rollbacks;
    ++deferred_checks_;
    return;
  }
  checked_rollbacks_ = rollbacks;
  deferred_checks_ = 0;
  try {
    states_.update(
        std::min(next_tick(), confirmed_tick()),
        [this](const auto tick) { return check_digest(tick); }, now);
    forget_checked_states();
  } catch (...) {
    log_divergence();
    throw;
  }
}

void MatchSession::reconcile() {
  prediction_.reconcile();
}

void MatchSession::collect_presentation() {
  if (!(players_ & (1U << room_.local_player()))) {
    return;
  }
  if (const auto presentation = match_.presentations[room_.local_player()].lock()) {
    presentation->collect(match_, prediction_);
  }
}

bool MatchSession::needs_input() const {
  return !frozen_ && !room_.recovering() && input_due();
}

bool MatchSession::input_due() const {
  return (players_ & (1U << room_.local_player())) &&
         (!captured_ || captured_->tick < prediction_.next_tick() + input_delay_);
}

void MatchSession::send(PlayerInput input, const double now) {
  // The ticks a match opens with, before the first captured controls act.
  for (auto tick = captured_ ? captured_->tick + 1 : prediction_.next_tick(); tick < input.tick;
       ++tick) {
    PlayerInput idle;
    idle.tick = tick;
    idle.player = input.player;
    inputs_.submit(idle, now);
    captured_ = idle;
  }
  inputs_.submit(input, now);
  captured_ = std::move(input);
}

void MatchSession::capture(const RobotControl &control, const FireControl &fire,
                           std::optional<TargetView> view, const double now) {
  if (!needs_input()) {
    throw std::runtime_error("Online tick already captured or local player is not in this match");
  }
  PlayerInput input;
  input.tick = prediction_.next_tick() + input_delay_;
  input.player = room_.local_player();
  input.control = control;
  input.fire = fire;
  input.view = std::move(view);
  send(std::move(input), now);
}

bool MatchSession::advance() {
  if (frozen_ || input_due()) {
    return false;
  }
  return prediction_.advance();
}

std::uint32_t MatchSession::next_tick() const {
  return prediction_.next_tick();
}
std::uint32_t MatchSession::confirmed_tick() const {
  return prediction_.confirmed_tick();
}
std::uint64_t MatchSession::rollback_count() const {
  return prediction_.rollback_count();
}

std::optional<StateDigest> MatchSession::confirmed_digest(const std::uint32_t tick) {
  std::optional<StateDigest> result;
  prediction_.inspect_confirmed(tick, [&] { result = state_digest(match_, &asset_digests_); });
  return result;
}

std::optional<StateDigest> MatchSession::check_digest(const std::uint32_t tick) {
  std::optional<StateDigest> result;
  prediction_.inspect_confirmed(tick, [&] {
    result = state_digest(match_, &asset_digests_);
    checked_states_[tick] = {simulation_.save(), input_lines(tick)};
  });
  return result;
}

std::vector<std::string> MatchSession::input_lines(const std::uint32_t tick) const {
  // Every retained confirmed tick since the previous check, a short digest of
  // each player's controls per tick, a dozen ticks a line.
  constexpr std::uint32_t span = 60, per_line = 12;
  std::vector<std::string> lines;
  std::string line;
  for (auto at = tick > span ? tick - span : 0; at < tick; ++at) {
    InputFrame frame;
    try {
      frame = prediction_.confirmed_frame(at);
    } catch (const std::runtime_error &) {
      continue; // already outside retained history
    }
    char text[16];
    std::snprintf(text, sizeof(text), " %u:", at);
    line += text;
    for (std::uint8_t player = 0; player < max_players; ++player) {
      if (!(players_ & (1U << player))) {
        continue;
      }
      const auto bytes = encode_input(frame.inputs[player]);
      std::uint32_t hash = 2166136261U; // FNV-1a
      for (const auto byte : bytes) {
        hash = (hash ^ byte) * 16777619U;
      }
      std::snprintf(text, sizeof(text), "%s%08x", line.back() == ':' ? "" : "/", hash);
      line += text;
    }
    if ((at + 1) % per_line == 0) {
      lines.push_back(std::move(line));
      line.clear();
    }
  }
  if (!line.empty()) {
    lines.push_back(std::move(line));
  }
  return lines;
}

void MatchSession::forget_checked_states() {
  constexpr std::size_t kept = 16;
  while (!checked_states_.empty() && (checked_states_.begin()->first <= states_.checked_tick() ||
                                      checked_states_.size() > kept)) {
    checked_states_.erase(checked_states_.begin());
  }
}

void MatchSession::log_divergence() {
  const auto &divergence = states_.divergence();
  if (!divergence || logged_divergence_) {
    return;
  }
  logged_divergence_ = true;
  try {
    const auto hex = [](const StateDigest &digest) {
      char text[17];
      std::snprintf(text, sizeof(text), "%02x%02x%02x%02x%02x%02x%02x%02x", digest[0], digest[1],
                    digest[2], digest[3], digest[4], digest[5], digest[6], digest[7]);
      return std::string(text);
    };
    const auto tick = divergence->tick;
    SDL_Log("Online divergence: tick=%u local player=%u peer player=%u local=%s peer=%s "
            "next=%u confirmed=%u rollbacks=%llu input_delay=%u",
            tick, room_.local_player() + 1U, divergence->peer + 1U, hex(divergence->local).c_str(),
            hex(divergence->remote).c_str(), next_tick(), confirmed_tick(),
            static_cast<unsigned long long>(rollback_count()), input_delay_);
    const auto found = checked_states_.find(tick);
    if (found == checked_states_.end()) {
      SDL_Log("Online divergence detail: tick=%u state no longer retained", tick);
      return;
    }
    for (const auto &line : found->second.inputs) {
      SDL_Log("Online divergence inputs:%s", line.c_str());
    }
    const auto present = simulation_.save();
    simulation_.restore(*found->second.state);
    std::vector<std::string> lines;
    try {
      lines = state_sections(match_);
    } catch (...) {
      simulation_.restore(*present);
      throw;
    }
    simulation_.restore(*present);
    for (const auto &line : lines) {
      SDL_Log("Online divergence detail: tick=%u %s", tick, line.c_str());
    }
  } catch (const std::exception &error) {
    SDL_Log("Online divergence detail failed: %s", error.what());
  }
}

void MatchSession::freeze() {
  frozen_ = true;
}

std::uint8_t MatchSession::confirmed_finished() {
  prediction_.reconcile(); // the present state must include every correction
  const auto tick = std::min(next_tick(), confirmed_tick());
  if (result_tick_ != tick) {
    const auto *players = &match_.players;
    if (tick != next_tick()) {
      const auto *state = prediction_.confirmed_state(tick);
      if (!state) {
        throw std::runtime_error("Confirmed match result is outside retained history");
      }
      players = &CoopMatch::players_in(*MatchSimulation::match_state(*state));
    }
    finished_ = 0;
    for (int slot = 0; slot < max_players; ++slot) {
      if ((*players)[slot].actor >= 0 && (*players)[slot].finished) {
        finished_ |= 1U << slot;
      }
    }
    result_tick_ = tick;
  }
  return finished_;
}

std::uint32_t MatchSession::frozen_horizon() const {
  // Controls already sent for ticks ahead must be played before aligning.
  return captured_ ? std::max(next_tick(), captured_->tick + 1) : next_tick();
}

bool MatchSession::align(const std::uint32_t target, const double now) {
  if (!frozen_ || target < frozen_horizon()) {
    throw std::runtime_error("Invalid online transition alignment");
  }
  if (room_.recovering()) {
    return false;
  }
  prediction_.reconcile();
  if (next_tick() < target) {
    if ((players_ & (1U << room_.local_player())) &&
        (!captured_ || captured_->tick < next_tick())) {
      auto input = predict_input(captured_.value_or(PlayerInput{}), next_tick());
      input.player = room_.local_player();
      send(std::move(input), now);
    }
    static_cast<void>(prediction_.advance());
  }
  collect_presentation();
  return next_tick() == target && confirmed_tick() >= target;
}
} // namespace phantomcrash::online
