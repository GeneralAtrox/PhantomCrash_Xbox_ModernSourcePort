// The thin lines painted into the menus' art, as data/ui/sprite_lines.json
// gives them: SVG path data flattened to polylines.

#include "test_support.hpp"

#include <algorithm>
#include <cmath>

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/sprite_lines.hpp"

using namespace phantomcrash;

namespace {

bool close_to(const float a, const float b) {
  return std::fabs(a - b) < 1e-3F;
}

} // namespace

TEST_CASE("a line path reads moves, lines and closes, absolute and relative") {
  const auto paths = parse_line_path("M 1 2 L 5 2 h 3 v 4 M 0 0 l 2,0 2 2 z");
  REQUIRE(paths.size() == 2U);
  REQUIRE(paths[0].points.size() == 4U);
  CHECK(!paths[0].closed);
  CHECK(close_to(paths[0].points[2][0], 8.0F));
  CHECK(close_to(paths[0].points[3][1], 6.0F));
  REQUIRE(paths[1].points.size() == 3U);
  CHECK(paths[1].closed);
  CHECK(close_to(paths[1].points[2][0], 4.0F));
  CHECK(close_to(paths[1].points[2][1], 2.0F));
}

TEST_CASE("a line path's curves flatten to within the tolerance") {
  const auto paths = parse_line_path("M 0 0 C 0 10 10 10 10 0", 0.05F);
  REQUIRE(paths.size() == 1U);
  const auto &points = paths[0].points;
  REQUIRE(points.size() > 8U);
  CHECK(close_to(points.back()[0], 10.0F));
  CHECK(close_to(points.back()[1], 0.0F));
  // The curve's middle is at three quarters of its controls' height.
  float highest = 0.0F;
  for (const auto &point : points) {
    highest = std::max(highest, point[1]);
  }
  CHECK(std::fabs(highest - 7.5F) < 0.1F);
}

TEST_CASE("a line path rejects what it cannot read") {
  CHECK_THROWS(parse_line_path("L 1 1"));
  CHECK_THROWS(parse_line_path("M 0 0 A 1 1 0 0 1 2 2"));
  CHECK_THROWS(parse_line_path("M 0 x"));
}

TEST_CASE("the shipped sprite lines lie inside their sprites") {
  const auto lines = parse_sprite_lines(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/sprite_lines.json"));
  REQUIRE(!lines.empty());
  for (const SpriteLines &entry : lines) {
    const float width = entry.rect[2] - entry.rect[0];
    const float height = entry.rect[3] - entry.rect[1];
    REQUIRE(!entry.strokes.empty());
    for (const LineStroke &stroke : entry.strokes) {
      REQUIRE(!stroke.paths.empty());
      for (const LinePolyline &path : stroke.paths) {
        for (const auto &point : path.points) {
          CHECK(point[0] >= -0.01F);
          CHECK(point[1] >= -0.01F);
          CHECK(point[0] <= width + 0.01F);
          CHECK(point[1] <= height + 0.01F);
        }
      }
    }
  }
}
