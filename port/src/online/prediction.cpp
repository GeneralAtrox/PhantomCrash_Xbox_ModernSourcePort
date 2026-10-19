#include "phantomcrash/online_prediction.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace phantomcrash::online {

Prediction::Prediction(Simulation &simulation, const std::uint8_t players,
                       const std::uint32_t first_tick, const std::uint32_t prediction_ticks,
                       const std::uint32_t history_ticks)
    : simulation_(simulation), players_(players), first_(first_tick), next_(first_tick),
      confirmed_(first_tick), prediction_ticks_(prediction_ticks), history_ticks_(history_ticks) {
  if (!players || players >= (1U << max_players) || prediction_ticks > history_ticks ||
      history_ticks == 0 || history_ticks > 600) {
    throw std::runtime_error("Invalid online prediction window");
  }
  for (std::uint8_t player = 0; player < max_players; ++player) {
    base_[player].player = player;
    base_[player].tick = first_tick;
  }
}

void Prediction::submit(const PlayerInput &input) {
  // Validate both local and remote sources identically before changing history.
  static_cast<void>(encode_input(input));
  if (!(players_ & (1U << input.player)) || input.tick < first_ ||
      (input.tick > next_ && input.tick - next_ > history_ticks_)) {
    throw std::runtime_error("Online input outside the session window");
  }
  if (!history_.empty() && input.tick < history_.front().frame.tick) {
    // These inputs were fully confirmed before their checkpoint was discarded.
    return;
  }
  auto &slot = received_[input.tick][input.player];
  if (slot) {
    if (!same_input(*slot, input)) {
      throw std::runtime_error("Conflicting confirmed online input");
    }
    return;
  }
  if (input.tick < next_) {
    const auto found = std::ranges::find_if(
        history_, [&](const Record &record) { return record.frame.tick == input.tick; });
    if (found == history_.end()) {
      throw std::runtime_error("Online input exceeds rollback history");
    }
    if (!same_input(found->frame.inputs[input.player], input)) {
      dirty_ = dirty_ ? std::min(*dirty_, input.tick) : input.tick;
    }
  }
  slot = input;
  update_confirmation();
}

void Prediction::update_confirmation() {
  for (;;) {
    const auto found = received_.find(confirmed_);
    if (found == received_.end()) {
      return;
    }
    for (std::uint8_t player = 0; player < max_players; ++player) {
      if ((players_ & (1U << player)) && !found->second[player]) {
        return;
      }
    }
    if (confirmed_ == std::numeric_limits<std::uint32_t>::max()) {
      throw std::runtime_error("Online session tick limit reached");
    }
    ++confirmed_;
  }
}

InputFrame Prediction::frame_at(const std::uint32_t tick) const {
  InputFrame frame;
  frame.tick = tick;
  frame.players = players_;
  const auto exact = received_.find(tick);
  for (std::uint8_t player = 0; player < max_players; ++player) {
    if (exact != received_.end() && exact->second[player]) {
      frame.inputs[player] = *exact->second[player];
      continue;
    }
    const PlayerInput *previous = &base_[player];
    auto candidate = received_.lower_bound(tick);
    while (candidate != received_.begin()) {
      --candidate;
      if (candidate->second[player]) {
        previous = &*candidate->second[player];
        break;
      }
    }
    frame.inputs[player] = predict_input(*previous, tick);
  }
  return frame;
}

void Prediction::reconcile() {
  if (!dirty_) {
    return;
  }
  const auto start = std::ranges::find_if(
      history_, [&](const Record &record) { return record.frame.tick == *dirty_; });
  if (start == history_.end()) {
    throw std::runtime_error("Online correction has no checkpoint");
  }
  simulation_.restore(*start->before);
  for (auto record = start; record != history_.end(); ++record) {
    record->before = simulation_.save();
    record->frame = frame_at(record->frame.tick);
    simulation_.step(record->frame);
  }
  dirty_.reset();
  ++rollbacks_;
}

bool Prediction::advance() {
  reconcile();
  if (next_ >= confirmed_ && next_ - confirmed_ >= prediction_ticks_) {
    return false;
  }
  if (next_ == std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error("Online session tick limit reached");
  }
  Record record{frame_at(next_), simulation_.save()};
  simulation_.step(record.frame);
  history_.push_back(std::move(record));
  ++next_;
  prune();
  return true;
}

bool Prediction::inspect_confirmed(const std::uint32_t tick, const std::function<void()> &inspect) {
  reconcile();
  if (tick > confirmed_ || tick > next_ || tick < first_) {
    return false;
  }
  if (tick == next_) {
    inspect();
    return true;
  }
  const auto record = std::ranges::find_if(
      history_, [tick](const Record &entry) { return entry.frame.tick == tick; });
  if (record == history_.end()) {
    return false;
  }
  const auto present = simulation_.save();
  simulation_.restore(*record->before);
  try {
    inspect();
  } catch (...) {
    simulation_.restore(*present);
    throw;
  }
  simulation_.restore(*present);
  return true;
}

const Simulation::Checkpoint *Prediction::confirmed_state(const std::uint32_t tick) {
  reconcile();
  if (tick > confirmed_ || tick >= next_ || tick < first_) {
    return nullptr;
  }
  const auto record = std::ranges::find_if(
      history_, [tick](const Record &entry) { return entry.frame.tick == tick; });
  return record == history_.end() ? nullptr : record->before.get();
}

void Prediction::prune() {
  while (history_.size() > history_ticks_) {
    const auto tick = history_.front().frame.tick;
    if (tick >= confirmed_) {
      throw std::runtime_error("Cannot discard an unconfirmed online tick");
    }
    base_ = history_.front().frame.inputs;
    received_.erase(tick);
    history_.pop_front();
  }
}

InputFrame Prediction::confirmed_frame(const std::uint32_t tick) const {
  if (tick >= std::min(next_, confirmed_) || !received_.contains(tick)) {
    throw std::runtime_error("Confirmed input history is unavailable");
  }
  return frame_at(tick);
}

std::uint32_t Prediction::next_tick() const {
  return next_;
}

std::uint32_t Prediction::confirmed_tick() const {
  return confirmed_;
}

std::uint64_t Prediction::rollback_count() const {
  return rollbacks_;
}

} // namespace phantomcrash::online
