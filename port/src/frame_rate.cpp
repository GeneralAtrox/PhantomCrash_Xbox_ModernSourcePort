#include "phantomcrash/frame_rate.hpp"
#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

namespace phantomcrash {
int snap_frame_cap(int position) {
  const int clamped = std::clamp(position, frame_cap_minimum, frame_cap_maximum);
  for (int point : frame_cap_snap_points) {
    if (std::abs(clamped - point) <= frame_cap_snap_radius) {
      return point;
    }
  }
  // Round to the nearest even step from the minimum.
  const int steps = (clamped - frame_cap_minimum + frame_cap_step / 2) / frame_cap_step;
  return std::min(frame_cap_maximum, frame_cap_minimum + steps * frame_cap_step);
}

std::optional<int> parse_frame_cap(std::wstring_view text) {
  while (!text.empty() && std::iswspace(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && std::iswspace(text.back())) {
    text.remove_suffix(1);
  }
  if (text.empty() || text.size() > 4) {
    return std::nullopt;
  }
  int value = 0;
  for (wchar_t c : text) {
    if (c < L'0' || c > L'9') {
      return std::nullopt;
    }
    value = value * 10 + (c - L'0');
  }
  return std::clamp(value, frame_cap_minimum, frame_cap_maximum);
}

FramePacer::FramePacer() {
#ifdef _WIN32
  // High-resolution waitable timers sleep to well under a millisecond; older
  // systems fall back to a coarse sleep followed by a short spin.
  timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                  TIMER_ALL_ACCESS);
#endif
}

FramePacer::~FramePacer() {
#ifdef _WIN32
  if (timer_) {
    CloseHandle(timer_);
  }
#endif
}

void FramePacer::reset() {
  next_.reset();
}

void FramePacer::wait(const FrameRateSettings &settings) {
  if (!settings.cap_enabled) {
    next_.reset();
    return;
  }
  const int cap = std::clamp(settings.cap, frame_cap_minimum, frame_cap_maximum);
  const auto period =
      std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / cap));
  const auto now = Clock::now();
  if (!next_ || cap != active_cap_ || *next_ + period < now) {
    // First capped frame, a new cap, or a stall longer than a frame: restart
    // the schedule instead of rendering a burst to catch up.
    active_cap_ = cap;
    next_ = now + period;
    return;
  }
  sleep_until(*next_);
  *next_ += period;
}

void FramePacer::sleep_until(Clock::time_point deadline) {
  using namespace std::chrono;
  constexpr auto spin_margin = microseconds(500);
  auto remaining = deadline - Clock::now();
#ifdef _WIN32
  if (timer_ && remaining > spin_margin) {
    LARGE_INTEGER due{};
    // Negative due times are relative, in 100 ns units.
    due.QuadPart = -duration_cast<nanoseconds>(remaining - spin_margin).count() / 100;
    if (SetWaitableTimerEx(timer_, &due, 0, nullptr, nullptr, nullptr, 0)) {
      WaitForSingleObject(timer_, INFINITE);
    }
  } else
#endif
      if (remaining > milliseconds(2)) {
    std::this_thread::sleep_for(remaining - milliseconds(2));
  }
  while (Clock::now() < deadline) {
    std::this_thread::yield();
  }
}
} // namespace phantomcrash
