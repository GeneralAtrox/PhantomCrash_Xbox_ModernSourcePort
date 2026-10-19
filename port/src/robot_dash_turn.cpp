#include "phantomcrash/robot_mover.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {

// 0x08f5d8: legs with dash-turn clips lean into a turn. The steer eases a
// sixteenth of the way to the stick each original tick; past the stick
// threshold to the right it picks clip 3, to the left 4, else the straight
// 2 (3 later dashing back). A change carries the frame over (0x08ddb2 is
// passed +0x120), so the stride does not start again.
void RobotMover::steer_dash(const float side, const float seconds) {
  const auto &legs = tuning_.dash_turn_legs;
  if (std::ranges::find(legs, leg_type_) == legs.end()) {
    return;
  }
  const float keep = 1.0F - 1.0F / tuning_.dash_turn_ease_ticks;
  const float ease = 1.0F - std::pow(keep, seconds * battle_tick_rate);
  dash_steer_ = std::clamp(dash_steer_ + (side - dash_steer_) * ease, -1.0F, 1.0F);
  int lean = 0;
  if (dash_steer_ >= tuning_.dash_turn_stick) {
    lean = 1;
  } else if (dash_steer_ <= -tuning_.dash_turn_stick) {
    lean = 2;
  }
  const int motion = (backward_ ? robot_motion::dash_back : robot_motion::dash) + lean;
  if (motion != motion_) {
    motion_ = motion; // the mode and its clock go on
    ++motion_revision_;
  }
}

} // namespace phantomcrash
