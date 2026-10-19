#pragma once
#include "desktop_coop_matches.hpp"
#include "phantomcrash/online_prediction.hpp"
#include <deque>
#include <functional>

namespace phantomcrash::online {
// Local presentation consumes each confirmed tick once. This queue is outside
// rollback state and survives membership changes while the view remains open.
class Presentation {
public:
  explicit Presentation(std::uint32_t first_tick) : next_(std::uint64_t(first_tick) + 1) {}
  void collect(CoopMatch &match, Prediction &prediction);
  void consume(CoopMatch &match, const std::function<void(std::uint32_t)> &present);
  [[nodiscard]] std::size_t pending() const {
    return frames_.size();
  }

private:
  struct Frame {
    std::uint32_t tick{};
    std::shared_ptr<const CoopMatch::Checkpoint> state;
  };
  std::uint64_t next_{};
  std::deque<Frame> frames_;
};
} // namespace phantomcrash::online
