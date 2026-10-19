#include "desktop_replay_overlay.hpp"
#include "desktop_textures.hpp"

namespace phantomcrash {
void record_quad(TextureCache &cache, SDL_Texture *texture, const SDL_FRect source,
                 const SDL_FRect target, const double angle, const SDL_FPoint pivot,
                 const SDL_FlipMode flip) {
  if (!cache.recording()) {
    return;
  }
  ReplayQuad quad;
  quad.texture = texture;
  quad.source = source;
  quad.target = target;
  quad.angle = angle;
  quad.pivot = pivot;
  quad.flip = flip;
  auto *renderer = cache.renderer();
  quad.clipped = SDL_RenderClipEnabled(renderer);
  SDL_GetRenderClipRect(renderer, &quad.clip);
  auto &c = quad.colour;
  if (texture) {
    SDL_GetTextureColorMod(texture, &c.r, &c.g, &c.b);
    SDL_GetTextureAlphaMod(texture, &c.a);
    SDL_GetTextureBlendMode(texture, &quad.blend);
  } else {
    SDL_GetRenderDrawColor(renderer, &c.r, &c.g, &c.b, &c.a);
    SDL_GetRenderDrawBlendMode(renderer, &quad.blend);
  }
  cache.recording()->push_back(quad);
}

void draw_replay_overlay(SDL_Renderer *renderer, const ReplayOverlay &overlay, const float scale) {
  for (const auto &q : overlay) {
    const SDL_FRect target{q.target.x * scale, q.target.y * scale, q.target.w * scale,
                           q.target.h * scale};
    const SDL_Rect clip{static_cast<int>(q.clip.x * scale), static_cast<int>(q.clip.y * scale),
                        static_cast<int>(q.clip.w * scale), static_cast<int>(q.clip.h * scale)};
    SDL_SetRenderClipRect(renderer, q.clipped ? &clip : nullptr);
    if (q.texture) {
      SDL_SetTextureColorMod(q.texture, q.colour.r, q.colour.g, q.colour.b);
      SDL_SetTextureAlphaMod(q.texture, q.colour.a);
      SDL_SetTextureBlendMode(q.texture, q.blend);
      const SDL_FPoint pivot{q.pivot.x * scale, q.pivot.y * scale};
      if (q.source.w == 0) {
        draw_texture_edge(renderer, q.texture, q.source, target);
      } else {
        SDL_RenderTextureRotated(renderer, q.texture, &q.source, &target, q.angle, &pivot, q.flip);
      }
    } else {
      SDL_SetRenderDrawColor(renderer, q.colour.r, q.colour.g, q.colour.b, q.colour.a);
      SDL_SetRenderDrawBlendMode(renderer, q.blend);
      SDL_RenderFillRect(renderer, &target);
    }
  }
  SDL_SetRenderClipRect(renderer, nullptr);
}
} // namespace phantomcrash
