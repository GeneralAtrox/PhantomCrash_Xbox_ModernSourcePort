// Data classes: shared members an entry names with "class".

#include "test_support.hpp"

#include "phantomcrash/json_classes.hpp"
#include "phantomcrash/sprite_shapes.hpp"
#include "phantomcrash/sprite_text.hpp"

#include <cmath>
#include <stdexcept>

using namespace phantomcrash;

namespace {

bool close_to(const float a, const float b) {
  return std::fabs(a - b) < 1e-4F;
}

} // namespace

TEST_CASE("an entry's classes supply members it leaves out, merged in order") {
  const json::JsonValue document = json::parse(R"({
    "classes": {
      "pill": {"font": "futura", "colour": "#ffcf00", "centered": true,
               "shadow": {"offset": [1, 1], "alpha": 0.6}},
      "lit": {"class": "pill", "colour": "#000000"}
    },
    "replacements": [
      {"source": {"image": "a.png", "rect": [0, 0, 64, 16]}, "text": "SAVE", "class": "pill"},
      {"source": {"image": "a.png", "rect": [0, 16, 64, 32]}, "text": "SAVE", "class": "lit",
       "shadow": {"alpha": 0.25}}
    ]})");
  const SpriteTextTable table = parse_sprite_text_table(document);
  REQUIRE(table.replacements.size() == 2U);
  const SpriteTextStyle &plain = table.replacements[0];
  CHECK(plain.font == "futura");
  CHECK(plain.centered);
  CHECK(plain.colour == 0xffcf00U);
  const SpriteTextStyle &lit = table.replacements[1];
  CHECK(lit.colour == 0x000000U);
  REQUIRE(lit.shadow.has_value());
  CHECK(close_to(lit.shadow->alpha, 0.25F));
  CHECK(close_to(lit.shadow->offset[0], 1.0F));
}

TEST_CASE("a shape and its outlines take their classes") {
  const auto shapes = parse_sprite_shapes(json::parse(R"({
    "classes": {
      "rim": {"box": [2, 2, 30, 14], "radius": 3, "stops": [[-1, "#151515"], [0, "#ffffff"]]},
      "pill": {"outlines": [{"class": "rim"}]}
    },
    "shapes": [{"image": "a.png", "rect": [0, 0, 32, 16], "class": "pill"}]})"));
  REQUIRE(shapes.size() == 1U);
  REQUIRE(shapes[0].outlines.size() == 1U);
  CHECK(close_to(shapes[0].outlines[0].radius, 3.0F));
  CHECK(close_to(shapes[0].outlines[0].box[2], 30.0F));
}

TEST_CASE("an unknown or self-naming class is refused") {
  const json::JsonValue classes = json::parse(R"({"loop": {"class": "loop"}})");
  bool unknown = false;
  try {
    (void)json::with_classes(json::parse(R"({"class": "missing"})"), &classes);
  } catch (const std::runtime_error &) {
    unknown = true;
  }
  CHECK(unknown);
  bool loop = false;
  try {
    (void)json::with_classes(json::parse(R"({"class": "loop"})"), &classes);
  } catch (const std::runtime_error &) {
    loop = true;
  }
  CHECK(loop);
}

TEST_CASE("lettering opacity and gradient stop alpha are read and clamped") {
  const SpriteTextTable table = parse_sprite_text_table(json::parse(R"({
    "classes": {"faint": {"font": "futura", "opacity": 0.5}},
    "replacements": [
      {"source": {"image": "a.png", "rect": [0, 0, 64, 16]}, "text": "A", "class": "faint"},
      {"source": {"image": "a.png", "rect": [0, 16, 64, 32]}, "text": "B", "opacity": 3},
      {"source": {"image": "a.png", "rect": [0, 32, 64, 48]}, "text": "C"}
    ]})"));
  REQUIRE(table.replacements.size() == 3U);
  CHECK(close_to(table.replacements[0].opacity, 0.5F));
  CHECK(close_to(table.replacements[1].opacity, 1.0F));
  CHECK(close_to(table.replacements[2].opacity, 1.0F));
  const auto shapes = parse_sprite_shapes(json::parse(R"({
    "shapes": [{"image": "a.png", "rect": [0, 0, 32, 16], "outlines": [
      {"box": [0, 0, 32, 16], "stops": [[0, "#ffffff"]],
       "fill": {"axis": "y", "stops": [[0, "#ffffff", 0.25], [16, "#000000"]]}}]}]})"));
  REQUIRE(shapes.size() == 1U);
  REQUIRE(shapes[0].outlines[0].fill.has_value());
  const auto &stops = shapes[0].outlines[0].fill->stops;
  REQUIRE(stops.size() == 2U);
  CHECK(close_to(stops[0].alpha, 0.25F));
  CHECK(close_to(stops[1].alpha, 1.0F));
}
