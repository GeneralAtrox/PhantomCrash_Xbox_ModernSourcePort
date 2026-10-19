#include "desktop_textures.hpp"

#include <algorithm>

namespace phantomcrash {

void draw_texture_edge(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_FRect &source,
                       const SDL_FRect &target) {
  float width = 0, height = 0;
  if (!SDL_GetTextureSize(texture, &width, &height) || width <= 0 || height <= 0) {
    return;
  }
  SDL_FColor tint;
  SDL_GetTextureColorModFloat(texture, &tint.r, &tint.g, &tint.b);
  SDL_GetTextureAlphaModFloat(texture, &tint.a);
  const float u = source.x / width;
  const float v0 = source.y / height, v1 = (source.y + source.h) / height;
  const float x0 = target.x, x1 = target.x + target.w;
  const float y0 = target.y, y1 = target.y + target.h;
  const SDL_Vertex vertices[] = {{{x0, y0}, tint, {u, v0}},
                                 {{x1, y0}, tint, {u, v0}},
                                 {{x1, y1}, tint, {u, v1}},
                                 {{x0, y1}, tint, {u, v1}}};
  const int indices[] = {0, 1, 2, 0, 2, 3};
  SDL_RenderGeometry(renderer, texture, vertices, 4, indices, 6);
}

void draw_sprite_edges(TextureCache &textures, SDL_Texture *texture, const Sprite &sprite,
                       const Viewport &viewport, SDL_FRect &source, SDL_FRect &target) {
  const auto strip = [&](const float x, const float width, const float texel, const int side) {
    if (width <= 0) {
      return;
    }
    const auto band = [&](const float begin, const float end) {
      const float top = std::max(source.y, sprite.rect[1] + begin);
      const float bottom = std::min(source.y + source.h, sprite.rect[1] + end);
      if (bottom <= top) {
        return;
      }
      const float scale = target.h / source.h;
      const SDL_FRect edge{texel, top, 0, bottom - top};
      const SDL_FRect area{x, target.y + (top - source.y) * scale, width, (bottom - top) * scale};
      record_quad(textures, texture, edge, area);
      draw_texture_edge(textures.renderer(), texture, edge, area);
    };
    if (sprite.edge_bands[side].empty()) {
      band(0, sprite.rect[3] - sprite.rect[1]);
    } else {
      for (const auto &range : sprite.edge_bands[side]) {
        band(range[0], range[1]);
      }
    }
  };
  const float texels = sprite.rect[2] - sprite.rect[0];
  if (texels <= 0) {
    return;
  }
  const float left = std::clamp(sprite.edge_inset[0], 0.0F, texels - 1);
  const float right = std::clamp(sprite.edge_inset[1], 0.0F, texels - 1);
  // Split at the visible texel centre, preserving the original UV-to-pixel mapping.
  // This replaces transparent padding rather than blending two draws over it.
  // Even with no padding, clamp at the texel centre: fractional viewport origins
  // otherwise let linear filtering pull a pale neighbour out of the atlas.
  const float scale = target.w / source.w;
  if (sprite.extend_left) {
    const float cut = std::max(0.0F, sprite.rect[0] + left + 0.5F - source.x);
    source.x += cut;
    source.w -= cut;
    target.x += cut * scale;
    target.w -= cut * scale;
  }
  if (sprite.extend_right) {
    const float cut = std::max(0.0F, source.x + source.w - (sprite.rect[2] - right - 0.5F));
    source.w -= cut;
    target.w -= cut * scale;
  }
  // Sample the centre of the edge texel: never bleed into another atlas sprite.
  if (sprite.extend_left) {
    strip(viewport.origin_x, target.x - viewport.origin_x, sprite.rect[0] + left + 0.5F, 0);
  }
  if (sprite.extend_right) {
    const float x = target.x + target.w;
    strip(x, viewport.origin_x + viewport.pixel_width - x, sprite.rect[2] - right - 0.5F, 1);
  }
}

} // namespace phantomcrash
