#pragma once

#include "desktop_textures.hpp"
#include "phantomcrash/canvas.hpp"
#include "phantomcrash/glyph_font.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace phantomcrash {

// Draws glyphs from the font atlas in `colour` at `alpha`, cut to `clip`
// (left, top, right, bottom in canvas units) when given.
void draw_glyphs(TextureCache &textures, const GlyphFont &font,
                 const std::vector<GlyphQuad> &glyphs, const Viewport &viewport,
                 std::uint32_t colour = 0xffffff,
                 const std::optional<std::array<float, 4>> &clip = std::nullopt,
                 float alpha = 1.0F);

} // namespace phantomcrash
