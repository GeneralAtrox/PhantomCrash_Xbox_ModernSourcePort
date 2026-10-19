#pragma once

namespace phantomcrash {

// The armour the gauges show (robot +0x50). On every change of the armour
// (+0x44) it eases in a straight line from where it stood to the new value
// (0x076368), over the change's share of the maximum times 90 frames, or
// times 300 frames for a change under a tenth; its timer (+0x58) loses
// the frames a tick, 2 at 30 ticks a second (0x075773). The gauges flash
// while it is below the armour, as it fills (0x04aaed).
class ShownArmour {
  friend class StateInspector;
public:
  // Follows `armour` of `max` by `seconds`.
  void tick(float armour, float max, float seconds);
  [[nodiscard]] float value() const noexcept {
    return shown_;
  }
  [[nodiscard]] bool filling() const noexcept {
    return shown_ < target_;
  }

private:
  bool started_{};
  float shown_{};
  float from_{};
  float target_{};
  float left_{};   // frames
  float length_{}; // frames
};

} // namespace phantomcrash
