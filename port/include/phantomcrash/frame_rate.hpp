#pragma once
#include <array>
#include <chrono>
#include <optional>
#include <string_view>

namespace phantomcrash {
// Presentation pacing only. Gameplay always advances on the fixed source
// tick; these settings decide how often interpolated frames are shown.
struct FrameRateSettings {
  bool vsync{};
  bool cap_enabled{};
  int cap{60};

  bool operator==(const FrameRateSettings &) const = default;
};

inline constexpr int frame_cap_minimum = 30;
inline constexpr int frame_cap_maximum = 144;
inline constexpr int frame_cap_step = 2;
// Common refresh rates the slider is drawn to, marked under the track.
inline constexpr std::array<int, 4> frame_cap_snap_points{30, 60, 120, 144};
inline constexpr int frame_cap_snap_radius = 3;

// Slider position -> stored value: steps of two, pulled to a nearby snap point.
int snap_frame_cap(int position);
// Typed value -> stored value: any whole number, clamped to the slider range.
std::optional<int> parse_frame_cap(std::wstring_view text);

// Sleeps until the next frame deadline when a cap is enabled.
class FramePacer final {
public:
  FramePacer();
  ~FramePacer();
  FramePacer(const FramePacer &) = delete;
  FramePacer &operator=(const FramePacer &) = delete;
  void wait(const FrameRateSettings &settings);
  // Starts a fresh schedule, e.g. after a pause or a settings change.
  void reset();

private:
  using Clock = std::chrono::steady_clock;
  void sleep_until(Clock::time_point deadline);
  std::optional<Clock::time_point> next_;
  int active_cap_{};
  void *timer_{};
};
} // namespace phantomcrash
