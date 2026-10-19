#pragma once

#include "phantomcrash/math3d.hpp"

#include <array>
#include <cstdint>

namespace phantomcrash {

// Desktop preferences. The original's own screen effects start on (battle
// setup turns the depth blur on, 0x0b2680(1)); the port's additions start off.
struct PostProcessOptions {
  bool depth_blur{true};
  bool distortion{true};
  bool colour_tint{true};
  bool smaa{};
  bool dynamic_lighting{}; // opt-in local lights and shadow maps
  // With dynamic lighting: the lights' beams and glow seen in the air.
  bool volumetric_light{true};
  // Opt-in: the softness of the original's 640x480 picture scaled to the
  // window, mixed over the native frame by softness_strength (0 to 1).
  bool original_softness{};
  float softness_strength{1.0F};
  // The game's text drawn from MS Gothic at the window's size instead of
  // its 18-pixel atlas: the same glyphs and spacing, sharp (data/ui/text.json).
  bool sharp_text{true};
  bool operator==(const PostProcessOptions &) const = default;
};

// Battle screen pass, before the cockpit and HUD (0x0b7900).
struct ScreenPass {
  Vec3 tint{1.0F, 1.0F, 1.0F};
  // The preset's levels (+0x34, 0x044974): each channel's bytes below
  // levels_min turn black and above levels_max white, the rest unchanged.
  Vec3 levels_min{0.0F, 0.0F, 0.0F};
  Vec3 levels_max{255.0F, 255.0F, 255.0F};
  bool depth_of_field{};
  float focus{80.0F}; // stage table 0x203820, before day adjustment
  float focus_scale{0.0005F};
  float focus_base{0.95F};
  float ramp{32.0F};
  float depth_near{0.1F};
  float depth_far{10000.0F};
  float blur_texels{1.0F};
  float day_focus_range{10.0F};
  bool distortion{};           // table 0x203840: daytime Shinjuku only
  float wave_amplitude{50.0F}; // 0x0b2770, signed offset byte
  float wave_phase_step{0.2F};
  float wave_update_hz{30.0F};
  float day_strength{};
  float softness{}; // the Original softness option's mix, set by selected_screen
  double seconds{}; // fixed game clock, also recorded by replay

  [[nodiscard]] bool levelled() const noexcept {
    return levels_min.x > 0.0F || levels_min.y > 0.0F || levels_min.z > 0.0F ||
           levels_max.x < 255.0F || levels_max.y < 255.0F || levels_max.z < 255.0F;
  }
  [[nodiscard]] bool active() const noexcept {
    return depth_of_field || distortion || tint.x != 1.0F || tint.y != 1.0F || tint.z != 1.0F ||
           levelled() || softness > 0.0F;
  }
};

// Displayed DAY-01 has stored_day 0. Called once at battle setup.
[[nodiscard]] ScreenPass screen_for_day(ScreenPass screen, int stored_day);
[[nodiscard]] ScreenPass selected_screen(ScreenPass screen, const PostProcessOptions &options);
// Reference-height intermediates, extended horizontally for widescreen.
[[nodiscard]] std::array<std::array<int, 2>, 2> screen_blur_sizes(int width, int height);
// Every column of the original 64x64 V8U8 offset map is identical; V is zero.
[[nodiscard]] std::array<std::int8_t, 64> screen_wave_rows(const ScreenPass &screen);

} // namespace phantomcrash
