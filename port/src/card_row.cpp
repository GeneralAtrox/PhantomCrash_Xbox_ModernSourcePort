#include "phantomcrash/card_row.hpp"

#include <algorithm>

namespace phantomcrash {

bool CardRow::move(int &first, int &row, const int step, const int count) {
  const int moved = row + step;
  if (moved < 0) {
    if (first == 0) {
      return false;
    }
    --first;
  } else if (moved >= style_.visible) {
    if (first + style_.visible >= count) {
      return false;
    }
    ++first;
  } else {
    row = moved;
    return true;
  }
  scroll(step);
  return true;
}

void CardRow::scroll(const int direction) {
  direction_ = direction < 0 ? -1 : 1;
  left_ = style_.scroll_seconds;
}

void CardRow::update(const double tick_seconds) {
  left_ = std::max(0.0, left_ - tick_seconds);
}

int CardRow::shown_first(const int first) const {
  return scrolling() && left_ > style_.scroll_seconds * 0.5 ? first - direction_ : first;
}

float CardRow::offset() const {
  if (!scrolling()) {
    return 0.0F;
  }
  const double along = 1.0 - left_ / std::max(style_.scroll_seconds, 1e-6);
  const float push = style_.push * 2.0F * static_cast<float>(direction_);
  if (along < 0.5) {
    return -push * static_cast<float>(along);
  }
  return push * static_cast<float>(1.0 - along);
}

} // namespace phantomcrash
