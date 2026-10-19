#pragma once

// Shared by the lock and the computer robots' fire (battle_targeting.cpp).
#include "phantomcrash/battle.hpp"

namespace phantomcrash {
// Whether `point` lies inside the view's clip volume: in its frustum.
[[nodiscard]] bool in_target_view(const TargetView &view, Vec3 point);
} // namespace phantomcrash
