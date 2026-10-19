#pragma once

#include "phantomcrash/json.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace phantomcrash {

// A path's points in the sprite's texels (rect-relative), its curves
// flattened; a closed one returns to its first point.
struct LinePolyline {
  std::vector<std::array<float, 2>> points;
  bool closed{};
};

// Thin lines painted into the art, drawn again over it at the output
// resolution: `width` texels wide in `colour`. The art under them, within
// `erase` texels of each line's centre, is filled from beside it first, so
// the painted line does not show round the new one.
struct LineStroke {
  float width{1.0F};
  std::uint32_t colour{0xffffff};
  float alpha{1.0F};
  float erase{};
  std::vector<LinePolyline> paths;
};

// data/ui/sprite_lines.json: a sprite's lines, found by its image and rect.
struct SpriteLines {
  std::string image;
  std::array<float, 4> rect{};
  std::vector<LineStroke> strokes;
};

// An SVG path's data (M, L, H, V, C, S, Q, T and Z, absolute or relative;
// no arcs), curves flattened to within `tolerance`. Throws
// std::runtime_error on anything else.
[[nodiscard]] std::vector<LinePolyline> parse_line_path(std::string_view data,
                                                        float tolerance = 0.02F);

[[nodiscard]] std::vector<SpriteLines> parse_sprite_lines(const json::JsonValue &document);

} // namespace phantomcrash
