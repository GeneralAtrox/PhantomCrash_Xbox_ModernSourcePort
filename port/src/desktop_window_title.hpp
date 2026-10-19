#pragma once

#include "desktop_screen.hpp"

#include <SDL3/SDL.h>

#include <optional>
#include <string>

namespace phantomcrash {

// The window's title: the port's name, the frame rate, and the movie or
// scene playing out with its frame of the total.
class WindowTitle final {
public:
  explicit WindowTitle(SDL_Window *window);

  // Once a presented frame; the title changes at most ten times a second.
  void update(double elapsed_seconds, double frames_per_second,
              const std::optional<SceneProgress> &playing);

private:
  SDL_Window *window_{};
  std::string name_;
  std::string shown_;
  double since_{};
};

} // namespace phantomcrash
