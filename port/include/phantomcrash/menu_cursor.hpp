#pragma once

#include "phantomcrash/input.hpp"

namespace phantomcrash {

// Cursor steps from a pair of directions, as the original's cursor helper
// (0xdf410) gives them: a new press steps at once, a held direction repeats
// after a delay. The repeat timing is the port's.
class CursorRepeat final {
public:
  CursorRepeat() = default;
  CursorRepeat(double delay, double interval) : delay_(delay), interval_(interval) {}

  // +1 towards `forward`, -1 towards `back`, or 0. `fresh` is set for a new
  // press, which may wrap around the ends where a repeat may not.
  int step(double tick_seconds, const InputFrame &input, Buttons back, Buttons forward,
           bool &fresh);
  void reset() noexcept {
    held_ = elapsed_ = 0.0;
  }

private:
  double delay_{};
  double interval_{};
  double held_{};
  double elapsed_{};
};

// The cursor after a step through `count` items: a fresh press wraps around
// the ends, a repeat stops at them.
[[nodiscard]] int move_cursor(int cursor, int step, bool fresh, int count);

} // namespace phantomcrash
