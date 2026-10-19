#pragma once

#include <string>
#include <vector>

namespace phantomcrash {

// Frame times gathered over a benchmark run (--benchmark), and what share
// of them met a frame budget.
class FrameStats final {
public:
  void add(double frame_seconds) {
    frames_.push_back(frame_seconds);
  }
  [[nodiscard]] std::size_t count() const noexcept {
    return frames_.size();
  }
  // The frame time `percent` of frames were at or under (0..100).
  [[nodiscard]] double percentile(double percent) const;
  // The share of frames at or under `budget_seconds` (0..1).
  [[nodiscard]] double within(double budget_seconds) const;
  // One line: frames, average rate, the 50th, 99th and 99.9th percentile
  // frame times and the worst, in milliseconds, and the share on time:
  // under 1.5 budgets, so not a refresh missed.
  [[nodiscard]] std::string report(double budget_seconds) const;

private:
  std::vector<double> frames_;
};

} // namespace phantomcrash
