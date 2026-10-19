#pragma once

#include <SDL3/SDL.h>

namespace phantomcrash {

// Frames per second over half-second windows, drawn in the top-left corner
// (Settings > Frame Rate Counter).
class FrameCounter final {
public:
  void record(double frame_seconds);
  void draw(SDL_Renderer *renderer) const;
  // The last half-second's rate, for the window title.
  [[nodiscard]] double rate() const {
    return rate_;
  }

private:
  double elapsed_{};
  int frames_{};
  double rate_{};
};

} // namespace phantomcrash
