#pragma once

// Split screen: the window divided between the robots the players control.
// Each screen draws the world from its robot's camera and its own HUD in a
// canvas of its own, as the original does with more than one screen
// (FUN_000ee540 + DAT_00416998 counts them): two screens are the top and
// bottom halves, 640x240 each; three or four are the quarters, 320x240 each.

#include "phantomcrash/canvas.hpp"

#include <array>
#include <vector>

namespace phantomcrash {

inline constexpr int max_screens = 4;

struct SplitScreen {
  PixelRect rect;                // in the window's pixels
  std::array<float, 2> canvas{}; // the screen's canvas in the original's units
};

// The screens for `count` players (clamped to 1-4) in a window of the given
// size. The screens are all the same size, so a pixel may be left over at
// the right or the bottom.
[[nodiscard]] std::vector<SplitScreen> split_screens(int count, int pixel_width, int pixel_height);

} // namespace phantomcrash
