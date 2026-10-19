#include "desktop_live_resize.hpp"

#include <SDL3/SDL_log.h>
#include <utility>

namespace phantomcrash {

LiveResizeTicks::LiveResizeTicks(std::function<bool()> active, std::function<void()> tick)
    : active_(std::move(active)), tick_(std::move(tick)) {
  if (!SDL_AddEventWatch(&LiveResizeTicks::watch, this)) {
    SDL_Log("Live-resize ticks unavailable: %s", SDL_GetError());
  }
}

LiveResizeTicks::~LiveResizeTicks() {
  SDL_RemoveEventWatch(&LiveResizeTicks::watch, this);
}

void LiveResizeTicks::rethrow() {
  if (failure_) {
    std::rethrow_exception(std::exchange(failure_, nullptr));
  }
}

bool SDLCALL LiveResizeTicks::watch(void *self, SDL_Event *event) {
  auto &ticks = *static_cast<LiveResizeTicks *>(self);
  // Only the modal loop's expose events (data1 == 1), never ordinary polling.
  if (event->type != SDL_EVENT_WINDOW_EXPOSED || event->window.data1 != 1 || ticks.running_ ||
      ticks.failure_ || !ticks.active_()) {
    return true;
  }
  ticks.running_ = true;
  try {
    ticks.tick_();
  } catch (...) {
    ticks.failure_ = std::current_exception();
  }
  ticks.running_ = false;
  return true;
}

void run_neutral_ticks(FixedStep &step, ScreenRunner &screens,
                       std::chrono::steady_clock::time_point &previous) {
  const auto now = std::chrono::steady_clock::now();
  const double elapsed = std::chrono::duration<double>(now - previous).count();
  previous = now;
  const int ticks = step.add(elapsed);
  for (int tick = 0; tick < ticks; ++tick) {
    screens.tick(InputFrame{}, step.tick_seconds());
  }
}

} // namespace phantomcrash
