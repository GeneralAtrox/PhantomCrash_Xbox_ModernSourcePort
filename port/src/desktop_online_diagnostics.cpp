#include "desktop_online_diagnostics.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace phantomcrash {
namespace {
constexpr const char *variable = "PHANGRADE_ONLINE_DIAGNOSTICS";
constexpr double report_seconds = 1.0;
// A frame that runs this many logic ticks or more is catching up on a backlog.
// At 144 Hz a normal frame runs at most one tick.
constexpr int catch_up_threshold = 2;
constexpr double milliseconds_per_second = 1000.0;
} // namespace

DesktopOnlineDiagnostics::DesktopOnlineDiagnostics() {
  const char *value = std::getenv(variable);
  enabled_ = value && std::strcmp(value, "1") == 0;
  if (enabled_) {
    SDL_Log("Online diagnostics: on (%s=1)", variable);
  }
}

void DesktopOnlineDiagnostics::frame(const double seconds, const int logic_ticks,
                                     const bool online) {
  if (!enabled_) {
    return;
  }
  if (!online) {
    window_ = {};
    return;
  }
  ++window_.frames;
  window_.seconds += seconds;
  window_.worst_seconds = std::max(window_.worst_seconds, seconds);
  window_.logic_ticks += logic_ticks;
  window_.max_ticks = std::max(window_.max_ticks, logic_ticks);
  if (logic_ticks >= catch_up_threshold) {
    ++window_.catch_up_frames;
  }
  if (window_.seconds >= report_seconds) {
    report();
  }
}

void DesktopOnlineDiagnostics::report() {
  const double fps = window_.frames / window_.seconds;
  const double ticks_per_frame = window_.logic_ticks / static_cast<double>(window_.frames);
  SDL_Log("Online diagnostics: %.1f fps, worst frame %.1f ms, %.2f logic ticks a frame "
          "(max %d), %d frames catching up",
          fps, window_.worst_seconds * milliseconds_per_second, ticks_per_frame,
          window_.max_ticks, window_.catch_up_frames);
  window_ = {};
}

} // namespace phantomcrash
