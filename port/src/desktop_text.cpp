#include "desktop_text.hpp"
#include "desktop_sharp_text.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {

void draw_glyphs(TextureCache &textures, const GlyphFont &font,
                 const std::vector<GlyphQuad> &glyphs, const Viewport &viewport,
                 const std::uint32_t colour, const std::optional<std::array<float, 4>> &clip,
                 const float alpha) {
  SDL_Texture *texture = textures.get(font.image);
  if (texture == nullptr || glyphs.empty() || alpha <= 0.0F) {
    return;
  }
  SDL_Renderer *renderer = textures.renderer();
  SharpText *sharp = textures.sharp_text();
  if (clip) {
    const auto [left, top, right, bottom] = *clip;
    const auto x0 = static_cast<int>(std::lround(viewport.to_pixel_x(left)));
    const auto y0 = static_cast<int>(std::lround(viewport.to_pixel_y(top)));
    const auto x1 = static_cast<int>(std::lround(viewport.to_pixel_x(right)));
    const auto y1 = static_cast<int>(std::lround(viewport.to_pixel_y(bottom)));
    const SDL_Rect pixels{x0, y0, x1 - x0, y1 - y0};
    SDL_SetRenderClipRect(renderer, &pixels);
  }
  SDL_SetTextureColorMod(texture, static_cast<Uint8>(colour >> 16U),
                         static_cast<Uint8>(colour >> 8U), static_cast<Uint8>(colour));
  SDL_SetTextureAlphaMod(texture, static_cast<Uint8>(std::min(alpha, 1.0F) * 255.0F + 0.5F));
  for (const auto &glyph : glyphs) {
    const auto [left, top, right, bottom] = glyph.rect;
    const SDL_FRect source{left, top, right - left, bottom - top};
    const auto pixels = viewport.to_pixels(glyph.x, glyph.y, glyph.width, glyph.height);
    const SDL_FRect target{pixels.x, pixels.y, pixels.width, pixels.height};
    record_quad(textures, texture, source, target);
    if (sharp == nullptr ||
        !sharp->draw(font, source, target, viewport.scale, colour, std::min(alpha, 1.0F))) {
      SDL_RenderTexture(renderer, texture, &source, &target);
    }
  }
  if (clip) {
    SDL_SetRenderClipRect(renderer, nullptr);
  }
}

} // namespace phantomcrash
