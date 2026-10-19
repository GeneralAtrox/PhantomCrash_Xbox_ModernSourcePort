#pragma once

#include "phantomcrash/online_input.hpp"
#include <array>
#include <deque>
#include <functional>
#include <map>
#include <memory>

namespace phantomcrash::online {
inline constexpr std::uint32_t default_prediction_ticks = 8;
// Local controls act this many ticks after they are captured, so remote ones
// arrive that much sooner and rollbacks replay fewer ticks (a port addition).
inline constexpr std::uint32_t default_input_delay_ticks = 1;
inline constexpr std::uint32_t max_input_delay_ticks = 4;

struct InputFrame {
  std::uint32_t tick{};
  std::uint8_t players{}; // bit mask of participants, stable for this timeline
  std::array<PlayerInput, max_players> inputs;
};

class Simulation {
public:
  struct Checkpoint {
    virtual ~Checkpoint() = default;
  };
  virtual ~Simulation() = default;
  [[nodiscard]] virtual std::unique_ptr<Checkpoint> save() const = 0;
  virtual void restore(const Checkpoint &state) = 0;
  virtual void step(const InputFrame &frame) = 0;
};

// Keeps prediction bounded and rewinds from the first mismatching remote input.
// Confirmed inputs are immutable. A conflicting retransmission is a session error.
// All tick numbers are absolute; a session must end before uint32 rollover.
class Prediction final {
public:
  Prediction(Simulation &simulation, std::uint8_t players, std::uint32_t first_tick = 0,
             std::uint32_t prediction_ticks = default_prediction_ticks,
             std::uint32_t history_ticks = 120);
  void submit(const PlayerInput &input);
  // Correct late inputs even if the presentation is not advancing a new tick.
  void reconcile();
  // Inspect the state immediately before a fully confirmed, retained tick.
  // The callback must only read the simulation. The predicted present is
  // restored even if it throws. False means that state is not available.
  bool inspect_confirmed(std::uint32_t tick, const std::function<void()> &inspect);
  // The saved state immediately before a fully confirmed, retained tick earlier
  // than next_tick(), read without restoring it. Null means it is not available.
  [[nodiscard]] const Simulation::Checkpoint *confirmed_state(std::uint32_t tick);
  // False means the prediction limit is reached; do not discard simulation ticks.
  bool advance();
  [[nodiscard]] std::uint32_t next_tick() const;
  [[nodiscard]] std::uint32_t confirmed_tick() const; // first not fully confirmed tick
  [[nodiscard]] std::uint64_t rollback_count() const;
  [[nodiscard]] InputFrame confirmed_frame(std::uint32_t tick) const;

private:
  struct Record {
    InputFrame frame;
    std::unique_ptr<Simulation::Checkpoint> before;
  };
  using Inputs = std::array<std::optional<PlayerInput>, max_players>;
  [[nodiscard]] InputFrame frame_at(std::uint32_t tick) const;
  void update_confirmation();
  void prune();
  Simulation &simulation_;
  std::uint8_t players_{};
  std::uint32_t first_{};
  std::uint32_t next_{};
  std::uint32_t confirmed_{};
  std::uint32_t prediction_ticks_{};
  std::uint32_t history_ticks_{};
  std::optional<std::uint32_t> dirty_;
  std::map<std::uint32_t, Inputs> received_;
  std::deque<Record> history_;
  std::array<PlayerInput, max_players> base_;
  std::uint64_t rollbacks_{};
};

} // namespace phantomcrash::online
