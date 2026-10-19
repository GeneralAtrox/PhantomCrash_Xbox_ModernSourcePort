#include "desktop_window_title.hpp"

#include <cmath>
#include <utility>

namespace phantomcrash {
namespace {

constexpr double refresh_seconds = 0.1;

} // namespace

WindowTitle::WindowTitle(SDL_Window *window) : window_(window) {
  if (const char *title = SDL_GetWindowTitle(window)) {
    name_ = title;
  }
  shown_ = name_;
}

void WindowTitle::update(const double elapsed_seconds, const double frames_per_second,
                         const std::optional<SceneProgress> &playing) {
  since_ += elapsed_seconds;
  if (since_ < refresh_seconds) {
    return;
  }
  since_ = 0.0;
  std::string title =
      name_ + " | " + std::to_string(static_cast<int>(std::lround(frames_per_second))) + " FPS";
  if (playing) {
    title += " | " + playing->label + ": " + playing->unit + " " + std::to_string(playing->frame) +
             " / " + std::to_string(playing->total);
  }
  if (title != shown_) {
    SDL_SetWindowTitle(window_, title.c_str());
    shown_ = std::move(title);
  }
}

} // namespace phantomcrash
