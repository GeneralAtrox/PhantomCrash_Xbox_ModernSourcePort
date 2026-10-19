#pragma once
#include "phantomcrash/frame_rate.hpp"

namespace phantomcrash {
// Modal "Frame Rate Cap" dialog. Returns true and updates `settings` when the
// user accepts; `owner` is the game window's HWND.
bool edit_frame_rate_cap(void *owner, FrameRateSettings &settings);
} // namespace phantomcrash
