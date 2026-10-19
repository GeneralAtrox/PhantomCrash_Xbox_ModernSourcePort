#pragma once

#include "desktop_screen.hpp"
#include "phantomcrash/sleep.hpp"

#include <SDL3/SDL.h>

namespace phantomcrash {

// Sleep (Sleep, scene 24): a black screen while the night passes.
class SleepScreen final : public Screen {
public:
  SleepScreen(SDL_Renderer *renderer, SleepScript script, QuestSave quest)
      : renderer_(renderer), sleep_(std::move(script), std::move(quest)) {}

  void tick(const InputFrame &input, const double tick_seconds) override {
    sleep_.tick(input, tick_seconds);
  }
  void draw(const Viewport & /*viewport*/) override {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
  }
  [[nodiscard]] std::vector<std::string> take_sounds() override {
    return sleep_.take_sounds();
  }
  [[nodiscard]] std::vector<std::string> held_sounds() const override {
    return {sleep_.script().sound};
  }
  [[nodiscard]] bool done() const override {
    return sleep_.done();
  }
  [[nodiscard]] std::string next() const override {
    return sleep_.script().exit_to;
  }
  [[nodiscard]] const QuestSave *quest_save() const override {
    return sleep_.done() ? &sleep_.quest() : nullptr;
  }

private:
  SDL_Renderer *renderer_{};
  Sleep sleep_;
};

} // namespace phantomcrash
