#pragma once

namespace phantomcrash {

// How a row of cards scrolls: the cards it shows at once, how long a scroll
// takes, and how far the cards are pushed at its half.
struct CardRowStyle {
  int visible{1};
  double scroll_seconds{};
  float push{};
};

// A row of cards seen through a window (KEEP SV's kept Scoobees, 0x0426a4;
// each VS player's Scoobees, 0x03f5d9). The cursor moves along the visible
// cards; past either end the row scrolls by one while there are cards that
// way: the cards are pushed away with the ones shown before, then come back
// in from the other side with the ones after.
class CardRow final {
public:
  explicit CardRow(const CardRowStyle &style = {}) : style_(style) {}

  // Moves the cursor (`first` visible card, `row` among the visible ones) a
  // card back (-1) or on (1) through `count`; false at that end.
  bool move(int &first, int &row, int step, int count);
  // Scrolls the way `first` has just moved (-1 back, 1 on).
  void scroll(int direction);
  void update(double tick_seconds);

  [[nodiscard]] bool scrolling() const noexcept {
    return left_ > 0.0;
  }
  // The way the row scrolls, -1 or 1, while it does; 0 otherwise.
  [[nodiscard]] int direction() const noexcept {
    return scrolling() ? direction_ : 0;
  }
  // The first card shown for the cursor's `first`: the one before the
  // scroll through its first half.
  [[nodiscard]] int shown_first(int first) const;
  // How far the cards are pushed along now.
  [[nodiscard]] float offset() const;

private:
  CardRowStyle style_;
  int direction_{};
  double left_{};
};

} // namespace phantomcrash
