#pragma once

#include "phantomcrash/json.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace phantomcrash {

// The original's system font (converted tables/font.json): a 512x2048 atlas
// of 18-texel cells, 28 to a row, holding the game's Shift-JIS character set
// in order. The first glyphs have their own left edge and width within the
// cell (proportional kana, digits and Latin letters); the rest fill it.
struct GlyphFont {
  std::string image;
  float cell{18.0F};
  int columns{28};
  std::unordered_map<char32_t, int> glyphs;  // character -> glyph (first occurrence)
  std::vector<char32_t> characters;          // glyph -> character
  std::vector<std::array<float, 2>> metrics; // left, width; later glyphs use 0, cell
  int fallback{};                            // the glyph for characters the font lacks
};

// Throws std::runtime_error when the table is malformed.
[[nodiscard]] GlyphFont parse_glyph_font(const json::JsonValue &document);

// The glyphs for UTF-8 text. The game writes its text in full-width forms, so
// ASCII maps to them (a space to the ideographic space U+3000); characters
// the font lacks become '?'.
[[nodiscard]] std::vector<int> glyph_run(const GlyphFont &font, std::string_view text);
[[nodiscard]] float run_width(const GlyphFont &font, const std::vector<int> &run);

struct GlyphQuad {
  std::array<float, 4> rect{}; // left, top, right, bottom in texels
  float x{}, y{}, width{}, height{};
};

// A run left to right from (x, y), each glyph advancing by its width.
[[nodiscard]] std::vector<GlyphQuad> layout_run(const GlyphFont &font, const std::vector<int> &run,
                                                float x, float y);
// Keep a label within its column, ending long labels with an ellipsis.
[[nodiscard]] std::vector<GlyphQuad> layout_label(const GlyphFont &font, std::string_view text,
                                                  float x, float y, float width);

// A glyph's whole cell at (x, y), as the original draws text in fixed
// 18 x 18 cells (0xbdc9b), and a run of them `advance` apart (0xbd720).
[[nodiscard]] GlyphQuad glyph_cell(const GlyphFont &font, int glyph, float x, float y);
[[nodiscard]] std::vector<GlyphQuad>
layout_cells(const GlyphFont &font, const std::vector<int> &run, float x, float y, float advance);

// Text that scrolls left through a clip rectangle and repeats (0x0be4b0):
// the help line and the current music on the option screen.
struct TickerStyle {
  std::array<float, 2> position{}; // where the text starts before it scrolls
  std::array<float, 4> clip{};     // left, top, right, bottom
  std::uint32_t colour{0xffffff};
  float speed{60.0F}; // units per second (one per frame at 60 Hz)
  float lead{};       // blank space before the text
  float trail{};      // and after it, before the next copy
  int layer{0x107};
};

// Throws std::runtime_error when a required value is missing or wrong.
[[nodiscard]] TickerStyle parse_ticker(const json::JsonValue &value);

// The glyphs of a ticker `seconds` after its text was set, cut to those that
// reach into the clip rectangle (the caller clips them to it).
[[nodiscard]] std::vector<GlyphQuad> place_ticker(const GlyphFont &font,
                                                  const std::vector<int> &run,
                                                  const TickerStyle &style, double seconds);

} // namespace phantomcrash
