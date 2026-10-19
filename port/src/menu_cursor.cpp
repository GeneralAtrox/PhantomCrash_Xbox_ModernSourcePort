#include "phantomcrash/menu_cursor.hpp"

#include <algorithm>

namespace phantomcrash {

int CursorRepeat::step(const double tick_seconds, const InputFrame &input, const Buttons back,
                       const Buttons forward, bool &fresh) {
  fresh = false;
  const bool forward_pressed = (input.pressed & forward) != 0;
  const bool back_pressed = (input.pressed & back) != 0;
  if (forward_pressed != back_pressed) {
    fresh = true;
    reset();
    return forward_pressed ? 1 : -1;
  }
  const bool forward_held = (input.held & forward) != 0;
  const bool back_held = (input.held & back) != 0;
  if (forward_held == back_held || interval_ <= 0.0) {
    reset();
    return 0;
  }
  held_ += tick_seconds;
  if (held_ < delay_) {
    return 0;
  }
  elapsed_ += tick_seconds;
  if (elapsed_ < interval_) {
    return 0;
  }
  elapsed_ -= interval_;
  return forward_held ? 1 : -1;
}

int move_cursor(const int cursor, const int step, const bool fresh, const int count) {
  if (step == 0 || count <= 0) {
    return cursor;
  }
  const int moved = cursor + step;
  if (moved >= 0 && moved < count) {
    return moved;
  }
  return fresh ? (moved + count) % count : std::clamp(moved, 0, count - 1);
}

} // namespace phantomcrash
