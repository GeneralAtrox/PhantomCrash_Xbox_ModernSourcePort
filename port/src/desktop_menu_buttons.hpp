#pragma once

#include "phantomcrash/canvas.hpp"
#include "phantomcrash/json.hpp"
#include <SDL3/SDL.h>
#include <memory>
#include <string>

namespace phantomcrash {

// Rasterizes vector outlines at the current output resolution and retains all
// three button states. Steady-state drawing only submits cached GPU textures.
class MenuButtons final {
public:
  MenuButtons(SDL_Renderer *renderer, const json::JsonValue &style);
  ~MenuButtons();
  void draw(const std::string &label, const std::array<float, 2> &position,
            const Viewport &viewport, bool selected, bool available, float selection_alpha);
  // Whether `point` (canvas units) is on the button drawn at `position`.
  [[nodiscard]] bool contains(const std::array<float, 2> &position,
                              const std::array<float, 2> &point) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace phantomcrash
