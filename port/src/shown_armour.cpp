#include "phantomcrash/shown_armour.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {
constexpr float frames_per_second = 60.0F;    // the timer's frames (0x075773)
constexpr float small_change = 0.1F;          // 0x17ac38
constexpr float small_change_frames = 300.0F; // 0x1b5ab8
constexpr float large_change_frames = 90.0F;  // 0x19ef0c
} // namespace

void ShownArmour::tick(const float armour, const float max, const float seconds) {
  if (!started_) {
    started_ = true;
    shown_ = from_ = target_ = armour;
    return;
  }
  if (armour != target_) {
    // 0x076368: from the shown value, over the change's share.
    const float share = max > 0.0F ? std::fabs(armour - shown_) / max : 0.0F;
    length_ = share * (share < small_change ? small_change_frames : large_change_frames);
    left_ = length_;
    from_ = shown_;
    target_ = armour;
  }
  left_ = std::max(left_ - seconds * frames_per_second, 0.0F);
  shown_ =
      left_ > 0.0F && length_ > 0.0F ? target_ + (from_ - target_) * (left_ / length_) : target_;
}

} // namespace phantomcrash
