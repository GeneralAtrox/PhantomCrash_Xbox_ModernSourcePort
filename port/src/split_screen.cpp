#include "phantomcrash/split_screen.hpp"

#include <algorithm>

namespace phantomcrash {
namespace {

// The canvas of one screen of two, and of one of three or four.
constexpr std::array<float, 2> half_canvas{canvas_width, canvas_height * 0.5F};
constexpr std::array<float, 2> quarter_canvas{canvas_width * 0.5F, canvas_height * 0.5F};

} // namespace

std::vector<SplitScreen> split_screens(const int count, const int pixel_width,
                                       const int pixel_height) {
  const auto width = static_cast<float>(std::max(pixel_width, 1));
  const auto height = static_cast<float>(std::max(pixel_height, 1));
  const int screens = std::clamp(count, 1, max_screens);
  if (screens == 1) {
    return {{{0.0F, 0.0F, width, height}, {canvas_width, canvas_height}}};
  }
  const auto half_height = static_cast<float>(std::max(pixel_height / 2, 1));
  if (screens == 2) {
    return {{{0.0F, 0.0F, width, half_height}, half_canvas},
            {{0.0F, half_height, width, half_height}, half_canvas}};
  }
  const auto half_width = static_cast<float>(std::max(pixel_width / 2, 1));
  std::vector<SplitScreen> out;
  for (int index = 0; index < screens; ++index) {
    const float x = index % 2 == 0 ? 0.0F : half_width;
    const float y = index < 2 ? 0.0F : half_height;
    out.push_back({{x, y, half_width, half_height}, quarter_canvas});
  }
  return out;
}

} // namespace phantomcrash
