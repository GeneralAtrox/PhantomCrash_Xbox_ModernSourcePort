#include "phantomcrash/post_process.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace phantomcrash {

ScreenPass screen_for_day(ScreenPass screen, const int stored_day) {
  const int day = std::max(stored_day, 0) % 10;
  screen.day_strength = static_cast<float>(std::abs(std::abs(day - 5) - 5)) / 5.0F;
  screen.focus -= screen.day_focus_range * screen.day_strength;
  return screen;
}

ScreenPass selected_screen(ScreenPass screen, const PostProcessOptions &options) {
  screen.depth_of_field &= options.depth_blur;
  screen.distortion &= options.distortion;
  screen.softness =
      options.original_softness ? std::clamp(options.softness_strength, 0.0F, 1.0F) : 0.0F;
  if (!options.colour_tint) {
    screen.tint = {1.0F, 1.0F, 1.0F};
    screen.levels_min = {0.0F, 0.0F, 0.0F};
    screen.levels_max = {255.0F, 255.0F, 255.0F};
  }
  return screen;
}

std::array<std::array<int, 2>, 2> screen_blur_sizes(const int width, const int height) {
  const double aspect = static_cast<double>(std::max(width, 1)) / std::max(height, 1);
  return {{{std::max(1, static_cast<int>(std::lround(240 * aspect))), 240},
           {std::max(1, static_cast<int>(std::lround(120 * aspect))), 120}}};
}

std::array<std::int8_t, 64> screen_wave_rows(const ScreenPass &screen) {
  std::array<std::int8_t, 64> rows{};
  if (screen.wave_phase_step <= 0 || screen.wave_update_hz <= 0) {
    return rows;
  }
  const double tick = std::floor(std::max(screen.seconds, 0.0) * screen.wave_update_hz + 1e-6);
  // The original updates the alternate buffer before sampling the previous one.
  // Start with a neutral map until one source tick has elapsed.
  if (tick < 1) {
    return rows;
  }
  const double cycle = std::floor(2 * std::numbers::pi / screen.wave_phase_step) + 1;
  const double phase = std::fmod(tick - 1, cycle) * screen.wave_phase_step;
  for (std::size_t row = 0; row < rows.size(); ++row) {
    const double wave = (std::sin(row * std::numbers::pi / 4 + phase) + 1) * 0.5;
    const double falloff = static_cast<double>(rows.size() - row) / rows.size();
    rows[row] =
        static_cast<std::int8_t>(std::clamp(wave * falloff * screen.wave_amplitude, 0.0, 127.0));
  }
  return rows;
}

} // namespace phantomcrash
