#pragma once

#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/json.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace phantomcrash {

// The outline font the game's text atlas was made from, and where its glyphs
// sit in an atlas cell (font cell units). The European disc's atlas is MS
// Gothic drawn 18 units high with its baseline 15 units down and the pen at
// the cell's left edge (data/ui/text.json).
struct SharpTextStyle {
  std::string face{"MS Gothic"};
  std::optional<std::filesystem::path> file; // a font file to use instead of an installed one
  float em{18.0F};
  float baseline{15.0F};
  float pen_x{};
  // Atlas glyphs that are the game's own art in Shift-JIS symbol places
  // (faces, zodiac signs, tools, kanji): drawn from the atlas as they are.
  std::unordered_set<int> pictures;
};

[[nodiscard]] SharpTextStyle parse_sharp_text(const json::JsonValue &document);

// The game's glyphs drawn from the outline font at the window's own size, so
// text keeps the atlas's glyphs, spacing and line breaks but is sharp at any
// resolution. Each glyph is drawn on first use into pages of cells, all
// redrawn when the size changes.
class SharpText final {
public:
  SharpText(SDL_Renderer *renderer, SharpTextStyle style);
  ~SharpText();
  SharpText(const SharpText &) = delete;
  SharpText &operator=(const SharpText &) = delete;

  // False when the font cannot be found; the atlas is drawn instead.
  [[nodiscard]] bool available() const noexcept {
    return available_;
  }
  // Draws `source` (a rectangle of the atlas, in texels, within one glyph
  // cell) over `target` (window pixels) at `scale` window pixels per texel.
  // False when the glyph cannot be drawn this way.
  bool draw(const GlyphFont &font, const SDL_FRect &source, const SDL_FRect &target, float scale,
            std::uint32_t colour, float alpha);

private:
  struct Slot {
    SDL_Texture *page{};
    int x{}, y{};
  };
  struct Impl; // the platform's rasterizer
  [[nodiscard]] const Slot *slot(const GlyphFont &font, int glyph);
  void reset(int cell, float font_cell);

  SDL_Renderer *renderer_{};
  SharpTextStyle style_;
  std::unique_ptr<Impl> impl_;
  bool available_{};
  int cell_{};        // pixels per glyph cell at the current size
  float scale_{1.0F}; // cell_ over the font's cell
  int slots_per_side_{};
  std::vector<SDL_Texture *> pages_;
  int used_{}; // slots taken in the last page
  std::unordered_map<int, Slot> slots_;
};

} // namespace phantomcrash
