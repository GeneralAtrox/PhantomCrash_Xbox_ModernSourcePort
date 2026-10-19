#pragma once

#include "phantomcrash/json.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

// One colour of an outline's profile, `distance` texels from its box's edge
// (negative inside). Two stops at one distance make a hard edge, drawn one
// screen pixel wide; inside the first stop its colour fills the box, and
// outside the last nothing is drawn.
// An outline with a `shade_toward` direction blends each point of its edge
// towards `shade_colour` and `shade_alpha` as the edge faces that way (an
// edge across it takes half), for bevels lit from one side.
struct ShapeStop {
  float distance{};
  std::uint32_t colour{0xffffff};
  float alpha{1.0F};
  std::uint32_t shade_colour{0xffffff};
  float shade_alpha{1.0F};
};

// A fill shaded along one axis: colours at positions (rect-relative texels)
// along x, or along y when `vertical`, linear between and held past the ends.
// Each stop's alpha scales the outline's, for glows that fade off a line.
struct GradientStop {
  float at{};
  std::uint32_t colour{};
  float alpha{1.0F};
};

struct ShapeGradient {
  bool vertical{true};
  std::vector<GradientStop> stops;
};

// A rounded rectangle (rect-relative LTRB, in the sprite's texels) coloured
// by distance from its edge, repeated over `count` cells `step` apart. Its
// corners' radius grows by `radius_growth` times the distance (1 keeps the
// edges parallel; the art's rings are often rounder inside than outside).
// A `fill` gradient colours the inside in place of the first stop's colour.
// With a `path` (a closed contour in the same texels, such as a folder's
// tabbed edge) the distances run from it instead, each point moved along
// its corner's bisector; `box` is then its bounds. A path's fill is a fan
// from its centre, so it must be seen whole from there; a gradient fill
// needs it convex. Distances deeper than a corner's radius fold there:
// layer outlines on exact inner contours rather than reach far in.
struct ShapeOutline {
  std::array<float, 4> box{};
  std::vector<std::array<float, 2>> path;
  float radius{};
  float radius_growth{1.0F};
  std::vector<ShapeStop> stops;
  std::optional<ShapeGradient> fill;
  std::optional<std::array<float, 2>> shade_toward; // unit length
  std::array<int, 2> count{1, 1};
  std::array<float, 2> step{};
};

// data/ui/sprite_shapes.json: plain frames and cursors the menus draw from
// their atlases, redrawn as outlines at the output resolution. Found by the
// sprite's image and rect.
struct SpriteShape {
  std::string image;
  std::array<float, 4> rect{};
  std::vector<ShapeOutline> outlines;
};

[[nodiscard]] std::vector<SpriteShape> parse_sprite_shapes(const json::JsonValue &document);
// A list of outlines as a shape's "outlines" give them (no classes), for
// shapes drawn on their own rather than over an atlas sprite.
[[nodiscard]] std::vector<ShapeOutline> parse_shape_outlines(const json::JsonValue &list);

// The outline's edge moved `distance` texels outward (inward when
// negative): `segments` + 1 points per corner, clockwise from the top-left
// corner's left end, the same count at every distance so contours pair up.
[[nodiscard]] std::vector<std::array<float, 2>> shape_contour(const ShapeOutline &outline,
                                                              const std::array<float, 4> &box,
                                                              float distance, int segments);

} // namespace phantomcrash
