#include "desktop_frame_counter.hpp"

#include <cstdio>

namespace phantomcrash {

void FrameCounter::record(const double frame_seconds) {
  elapsed_ += frame_seconds;
  ++frames_;
  if (elapsed_ >= 0.5) {
    rate_ = frames_ / elapsed_;
    elapsed_ = 0.0;
    frames_ = 0;
  }
}

void FrameCounter::draw(SDL_Renderer *renderer) const {
  char line[32];
  std::snprintf(line, sizeof(line), "%.0f FPS", rate_);
  SDL_SetRenderScale(renderer, 2.0F, 2.0F);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
  const SDL_FRect back{2, 2, 8.0F * 8 + 4, 12};
  SDL_RenderFillRect(renderer, &back);
  SDL_SetRenderDrawColor(renderer, 120, 255, 120, 255);
  SDL_RenderDebugText(renderer, 4, 4, line);
  SDL_SetRenderScale(renderer, 1.0F, 1.0F);
}

} // namespace phantomcrash
