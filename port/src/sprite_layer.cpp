#include "phantomcrash/sprite_layer.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace phantomcrash {
namespace {

using json::numbers;
using json::required;

// The original counts frames: t = (frame % 81) / 80.
constexpr double cycle_seconds = 81.0 / 60.0;
constexpr double ramp_seconds = 80.0 / 60.0;

Animation parse_animation(const std::string &name) {
  if (name == "none") {
    return Animation::none;
  }
  if (name == "fade") {
    return Animation::fade;
  }
  if (name == "pulse") {
    return Animation::pulse;
  }
  if (name == "blink") {
    return Animation::blink;
  }
  throw std::runtime_error("animation must be none, fade, pulse or blink, not \"" + name + "\"");
}

SpriteRepeat parse_repeat(const json::JsonValue &value) {
  SpriteRepeat repeat;
  repeat.count = static_cast<int>(required(value, "count").number());
  if (repeat.count < 1) {
    throw std::runtime_error("repeat count must be at least 1");
  }
  repeat.step = numbers<2>(required(value, "step"), "step");
  repeat.wrap = static_cast<float>(value.number_or("wrap", 0.0));
  if (const auto *scroll = value.find("scroll_per_second")) {
    repeat.scroll_per_second = numbers<2>(*scroll, "scroll_per_second");
  }
  repeat.scroll_reset = static_cast<float>(value.number_or("scroll_reset", 0.0));
  return repeat;
}

float wrapped(const float value, const float wrap) {
  if (wrap <= 0.0F) {
    return value;
  }
  const float result = std::fmod(value, wrap);
  return result < 0.0F ? result + wrap : result;
}

} // namespace

float animation_alpha(const Animation animation, const double seconds) {
  if (animation == Animation::none) {
    return 1.0F;
  }
  const double t = std::min(std::fmod(std::max(seconds, 0.0), cycle_seconds) / ramp_seconds, 1.0);
  const auto wave = static_cast<float>(std::abs(2.0 * t - 1.0));
  switch (animation) {
  case Animation::fade:
    return wave;
  case Animation::pulse:
    return 0.3F + 0.7F * wave;
  case Animation::blink:
    return wave >= 0.5F ? 1.0F : 0.0F;
  default:
    return 1.0F;
  }
}

std::uint32_t parse_colour(const std::string &text) {
  std::uint32_t colour = 0;
  const char *begin = text.data() + 1;
  const char *end = text.data() + text.size();
  if (text.size() != 7 || text[0] != '#' || std::from_chars(begin, end, colour, 16).ptr != end) {
    throw std::runtime_error("colour must be #rrggbb, not \"" + text + "\"");
  }
  return colour;
}

void parse_sprite_edges(const json::JsonValue &value, Sprite &sprite) {
  const std::string edges = value.string_or("extend_edges", "none");
  if (edges != "none" && edges != "left" && edges != "right" && edges != "both") {
    throw std::runtime_error("extend_edges must be none, left, right or both");
  }
  sprite.extend_left = edges == "left" || edges == "both";
  sprite.extend_right = edges == "right" || edges == "both";
  if (const auto *inset = value.find("edge_inset")) {
    sprite.edge_inset = numbers<2>(*inset, "edge_inset");
  }
  const std::array<const char *, 2> band_keys{"edge_bands_left", "edge_bands_right"};
  for (std::size_t side = 0; side < band_keys.size(); ++side) {
    const auto *bands = value.find(band_keys[side]);
    if (bands == nullptr) {
      bands = value.find("edge_bands");
    }
    if (bands != nullptr) {
      for (const auto &band : bands->array()) {
        sprite.edge_bands[side].push_back(numbers<2>(band, band_keys[side]));
      }
    }
  }
}

Sprite parse_sprite(const json::JsonValue &value) {
  Sprite sprite;
  sprite.id = value.string_or("id", "");
  try {
    sprite.image = required(value, "image").string();
    sprite.rect = numbers<4>(required(value, "rect"), "rect");
    if (const auto *position = value.find("position")) {
      sprite.position = numbers<2>(*position, "position");
    }
    if (const auto *size = value.find("size")) {
      sprite.size = numbers<2>(*size, "size");
    }
    sprite.centered = value.boolean_or("centered", false);
    parse_sprite_edges(value, sprite);
    sprite.layer = static_cast<int>(value.number_or("layer", 0x101));
    if (const auto *colour = value.find("colour")) {
      sprite.colour = parse_colour(colour->string());
    }
    sprite.animation = parse_animation(value.string_or("animation", "none"));
    if (const auto *repeat = value.find("repeat")) {
      sprite.repeat = parse_repeat(*repeat);
    }
    sprite.title_only = value.boolean_or("title_only", false);
    sprite.flip_x = value.boolean_or("flip_x", false);
    sprite.angle = static_cast<float>(value.number_or("angle", 0.0));
    if (const auto *pivot = value.find("pivot")) {
      sprite.pivot = numbers<2>(*pivot, "pivot");
    }
    if (const auto *fallback = value.find("fallback")) {
      sprite.fallback = parse_sprites(*fallback);
    }
  } catch (const std::exception &error) {
    throw std::runtime_error("sprite \"" + sprite.id + "\": " + error.what());
  }
  return sprite;
}

std::vector<Sprite> parse_sprites(const json::JsonValue &array) {
  std::vector<Sprite> sprites;
  for (const auto &value : array.array()) {
    sprites.push_back(parse_sprite(value));
  }
  return sprites;
}

std::vector<Quad> place_sprite(const Sprite &sprite, const double seconds) {
  const auto [left, top, right, bottom] = sprite.rect;
  const float width = sprite.size ? (*sprite.size)[0] : right - left;
  const float height = sprite.size ? (*sprite.size)[1] : bottom - top;
  const SpriteRepeat single;
  const SpriteRepeat &repeat = sprite.repeat ? *sprite.repeat : single;
  std::array<float, 2> scroll{};
  for (int axis = 0; axis < 2; ++axis) {
    scroll[axis] = static_cast<float>(repeat.scroll_per_second[axis] * seconds);
    scroll[axis] = wrapped(scroll[axis], repeat.scroll_reset);
  }
  std::vector<Quad> quads;
  quads.reserve(static_cast<std::size_t>(repeat.count));
  for (int index = 0; index < repeat.count; ++index) {
    float x = sprite.position[0];
    float y = sprite.position[1];
    const float along_x = scroll[0] + repeat.step[0] * static_cast<float>(index);
    const float along_y = scroll[1] + repeat.step[1] * static_cast<float>(index);
    // Only the stepped axis wraps, as the scanlines do (y modulo 480).
    x += repeat.step[0] != 0.0F ? wrapped(along_x, repeat.wrap) : along_x;
    y += repeat.step[1] != 0.0F ? wrapped(along_y, repeat.wrap) : along_y;
    if (sprite.centered) {
      x -= width * 0.5F;
      y -= height * 0.5F;
    }
    quads.push_back({x, y, width, height});
  }
  return quads;
}

Sprite scaled_sprite(Sprite sprite, const float x, const float y, const ScaleAnchor anchor) {
  const std::array<float, 2> size = sprite.size.value_or(
      std::array<float, 2>{sprite.rect[2] - sprite.rect[0], sprite.rect[3] - sprite.rect[1]});
  std::array<float, 2> left_top = sprite.position;
  if (sprite.centered) {
    left_top = {left_top[0] - size[0] * 0.5F, left_top[1] - size[1] * 0.5F};
  }
  // How far along the lost width and height the top-left moves.
  float along_x = 0.5F;
  float along_y = 0.5F;
  switch (anchor) {
  case ScaleAnchor::centre:
    break;
  case ScaleAnchor::bottom_right:
    along_x = along_y = 1.0F;
    break;
  case ScaleAnchor::bottom_left:
    along_x = 0.0F;
    along_y = 1.0F;
    break;
  case ScaleAnchor::top_right:
    along_x = 1.0F;
    along_y = 0.0F;
    break;
  case ScaleAnchor::top_left:
    along_x = along_y = 0.0F;
    break;
  }
  sprite.position = {left_top[0] + size[0] * (1.0F - x) * along_x,
                     left_top[1] + size[1] * (1.0F - y) * along_y};
  sprite.size = std::array<float, 2>{size[0] * x, size[1] * y};
  sprite.centered = false;
  return sprite;
}

} // namespace phantomcrash
