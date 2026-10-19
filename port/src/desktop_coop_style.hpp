#pragma once

#include "phantomcrash/canvas.hpp"

#include <SDL3/SDL.h>

// The Co-op screens use the game's own menu look: the save and load screens'
// photograph backdrop and EMPTY card, MenuButtons for the menu rows, and the
// same dark plates, white outlines and gold selection (data/ui/main_menu_buttons.json).
// Plates and rules are simple vector shapes; every label is text.
namespace phantomcrash::coop_style {

inline constexpr SDL_Color ink{243, 244, 235, 255};     // outlines and main text (#f3f4eb)
inline constexpr SDL_Color accent{255, 231, 0, 255};    // headings (#ffe700)
inline constexpr SDL_Color gold{232, 189, 0, 255};      // selected row (#e8bd00)
inline constexpr SDL_Color on_gold{2, 3, 2, 255};       // text on a selected row (#020302)
inline constexpr SDL_Color panel{2, 3, 2, 255};         // dark plate (#020302)
inline constexpr SDL_Color dim{190, 194, 178, 255};     // secondary text
inline constexpr SDL_Color ready{120, 230, 140, 255};   // a bay that is ready
inline constexpr SDL_Color alarm{255, 120, 100, 255};   // failed save or link

// A canvas rectangle filled in `colour` at `alpha`.
inline void fill(SDL_Renderer *renderer, const Viewport &v, float x, float y, float width,
                 float height, SDL_Color colour, Uint8 alpha = 255) {
  const PixelRect pixels = v.to_pixels(x, y, width, height);
  const SDL_FRect rect{pixels.x, pixels.y, pixels.width, pixels.height};
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, colour.r, colour.g, colour.b, alpha);
  SDL_RenderFillRect(renderer, &rect);
}

// A canvas rectangle's outline in `colour`.
inline void outline(SDL_Renderer *renderer, const Viewport &v, float x, float y, float width,
                    float height, SDL_Color colour) {
  const PixelRect pixels = v.to_pixels(x, y, width, height);
  const SDL_FRect rect{pixels.x, pixels.y, pixels.width, pixels.height};
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, colour.r, colour.g, colour.b, 255);
  SDL_RenderRect(renderer, &rect);
}

// A dark plate with the game's white outline, behind text.
inline void panel_box(SDL_Renderer *renderer, const Viewport &v, float x, float y, float width,
                      float height) {
  fill(renderer, v, x, y, width, height, panel, 215);
  outline(renderer, v, x, y, width, height, ink);
}

// A row picked out in the game's gold, its text then drawn in on_gold.
inline void selection(SDL_Renderer *renderer, const Viewport &v, float x, float y, float width,
                      float height) {
  fill(renderer, v, x, y, width, height, gold);
}

// Seconds for the backdrop's animations, as the screens use them.
inline double art_seconds() {
  return static_cast<double>(SDL_GetTicks()) / 1000.0;
}

} // namespace phantomcrash::coop_style
