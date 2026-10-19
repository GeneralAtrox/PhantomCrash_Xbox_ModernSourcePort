#pragma once

#include "desktop_screens.hpp"
#include "phantomcrash/fixed_step.hpp"

#include <SDL3/SDL_events.h>
#include <chrono>
#include <exception>
#include <functional>

namespace phantomcrash {

// Moving or resizing the window, or opening its system menu, runs a Windows
// modal loop inside SDL_PollEvent, so the frame loop stops. SDL keeps sending
// live-resize expose events from inside that loop. While `active` holds,
// those run `tick`, so online play keeps producing (neutral) controls and the
// other players do not wait at their prediction limit (as Lodoss's online
// loop does). Offline play keeps stopping, as the original pauses.
class LiveResizeTicks final {
public:
  LiveResizeTicks(std::function<bool()> active, std::function<void()> tick);
  ~LiveResizeTicks();
  LiveResizeTicks(const LiveResizeTicks &) = delete;
  LiveResizeTicks &operator=(const LiveResizeTicks &) = delete;
  // Nothing may unwind through SDL's window procedure: a failed tick is kept
  // and thrown here, after SDL_PollEvent returns.
  void rethrow();

private:
  static bool SDLCALL watch(void *self, SDL_Event *event);
  std::function<bool()> active_;
  std::function<void()> tick_;
  std::exception_ptr failure_;
  bool running_{};
};

// The ticks due since `previous` with no controls held; `previous` becomes now.
void run_neutral_ticks(FixedStep &step, ScreenRunner &screens,
                       std::chrono::steady_clock::time_point &previous);

} // namespace phantomcrash
