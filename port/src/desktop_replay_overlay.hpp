#pragma once
#include <SDL3/SDL.h>
#include <vector>

namespace phantomcrash {
class TextureCache;
// The actual submitted 2D quads, in presentation pixels and submission order.
// Texture handles belong to TextureCache, which outlives the recorded match.
struct ReplayQuad {
  SDL_Texture *texture{};
  SDL_FRect source{}, target{};
  SDL_FPoint pivot{};
  SDL_Rect clip{};
  SDL_Color colour{};
  SDL_BlendMode blend{};
  SDL_FlipMode flip{};
  double angle{};
  bool clipped{};
};
using ReplayOverlay = std::vector<ReplayQuad>;
void record_quad(TextureCache &cache, SDL_Texture *texture, SDL_FRect source, SDL_FRect target,
                 double angle = 0, SDL_FPoint pivot = {}, SDL_FlipMode flip = SDL_FLIP_NONE);
void draw_replay_overlay(SDL_Renderer *renderer, const ReplayOverlay &overlay, float scale);
} // namespace phantomcrash
