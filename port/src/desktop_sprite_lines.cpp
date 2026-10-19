#include "desktop_sprite_lines.hpp"

#include <algorithm>
#include <cmath>
#include <exception>

namespace phantomcrash {

namespace {

using Point = std::array<float, 2>;

// Miter joins stop at this many half widths, so a sharp turn cannot spike.
constexpr float miter_limit = 4.0F;

Point normal_of(const Point &a, const Point &b) {
  const float dx = b[0] - a[0], dy = b[1] - a[1];
  const float length = std::hypot(dx, dy);
  return length > 0.0F ? Point{-dy / length, dx / length} : Point{0.0F, 0.0F};
}

// A polyline in pixels; an end on the sprite's left or right side is cut
// square there (`flush`), as the art is.
struct PixelPath {
  std::vector<Point> points;
  std::array<bool, 2> flush{};
};

// The polyline in pixels, repeated points dropped, a closed one without its
// repeated first point.
PixelPath to_pixels(const LinePolyline &path, const LinePlacement &placement, const float scale_x,
                    const float scale_y) {
  PixelPath out;
  std::vector<Point> &points = out.points;
  const auto add = [&](const Point &p) {
    if (points.empty() || std::hypot(p[0] - points.back()[0], p[1] - points.back()[1]) > 0.05F) {
      points.push_back(p);
    }
  };
  for (const Point &p : path.points) {
    add({placement.target.x + p[0] * scale_x, placement.target.y + p[1] * scale_y});
  }
  if (path.closed && points.size() > 2 &&
      std::hypot(points.front()[0] - points.back()[0], points.front()[1] - points.back()[1]) <
          0.05F) {
    points.pop_back();
  }
  if (path.closed || points.size() < 2) {
    return out;
  }
  // An end on a side runs on along the stretch when the side is stretched.
  const float left = placement.target.x + 0.01F * scale_x;
  const float right = placement.target.x + placement.target.w - 0.01F * scale_x;
  for (const bool front : {true, false}) {
    Point run = front ? points.front() : points.back();
    if (run[0] <= left) {
      run[0] = placement.reach_left.value_or(run[0]);
    } else if (run[0] >= right) {
      run[0] = placement.reach_right.value_or(run[0]);
    } else {
      continue;
    }
    out.flush[front ? 0 : 1] = true;
    if (!front) {
      add(run);
    } else if (std::hypot(run[0] - points.front()[0], run[1] - points.front()[1]) > 0.05F) {
      points.insert(points.begin(), run);
    }
  }
  return out;
}

// One polyline as four rails either side of its centre: the outer pair
// transparent, the inner pair the colour, a pixel apart, so the edges are
// antialiased; open ends fade over a pixel past half the width, unless
// flush.
void stroke(const PixelPath &path, const bool closed, const float half, const SDL_FColor colour,
            std::vector<SDL_Vertex> &vertices, std::vector<int> &indices) {
  const std::vector<Point> &points = path.points;
  const std::size_t count = points.size();
  if (count < 2) {
    return;
  }
  const float inner = std::max(half - 0.5F, 0.0F);
  const float outer = half + 0.5F;
  const float core = std::min(1.0F, 2.0F * half); // a line under a pixel wide fades
  SDL_FColor solid = colour, clear = colour;
  solid.a *= core;
  clear.a = 0.0F;
  struct Rail {
    Point at, normal;
    float scale;
    bool faded;
  };
  std::vector<Rail> rails;
  for (std::size_t i = 0; i < count; ++i) {
    const bool first = i == 0, last = i + 1 == count;
    const Point &prev = first ? (closed ? points[count - 1] : points[i]) : points[i - 1];
    const Point &next = last ? (closed ? points[0] : points[i]) : points[i + 1];
    Point n_in = normal_of(prev, points[i]);
    Point n_out = normal_of(points[i], next);
    if (!closed && first) {
      n_in = n_out;
    }
    if (!closed && last) {
      n_out = n_in;
    }
    Point miter{n_in[0] + n_out[0], n_in[1] + n_out[1]};
    const float length = std::hypot(miter[0], miter[1]);
    miter = length > 1e-4F ? Point{miter[0] / length, miter[1] / length} : n_out;
    const float dot = miter[0] * n_out[0] + miter[1] * n_out[1];
    const float scale = std::min(1.0F / std::max(dot, 1e-3F), miter_limit);
    rails.push_back({points[i], miter, scale, false});
  }
  if (!closed) {
    // Square ends half the width on, then a pixel's fade.
    for (const bool front : {true, false}) {
      if (path.flush[front ? 0 : 1]) {
        continue;
      }
      const Point &end = front ? points[0] : points[count - 1];
      const Point &before = front ? points[1] : points[count - 2];
      const float dx = end[0] - before[0], dy = end[1] - before[1];
      const float length = std::max(std::hypot(dx, dy), 1e-4F);
      const Point along{dx / length, dy / length};
      Rail &rail = front ? rails.front() : rails.back();
      rail.at = {end[0] + along[0] * inner, end[1] + along[1] * inner};
      Rail fade{
          {end[0] + along[0] * outer, end[1] + along[1] * outer}, rail.normal, rail.scale, true};
      if (front) {
        rails.insert(rails.begin(), fade);
      } else {
        rails.push_back(fade);
      }
    }
  }
  const int base = static_cast<int>(vertices.size());
  const std::array<float, 4> offsets{-outer, -inner, inner, outer};
  for (const Rail &rail : rails) {
    for (std::size_t k = 0; k < offsets.size(); ++k) {
      const float d = offsets[k] * rail.scale;
      const bool edge = k == 0 || k == 3 || rail.faded;
      vertices.push_back({{rail.at[0] + rail.normal[0] * d, rail.at[1] + rail.normal[1] * d},
                          edge ? clear : solid,
                          {}});
    }
  }
  const int rows = static_cast<int>(rails.size());
  const int segments = closed ? rows : rows - 1;
  for (int i = 0; i < segments; ++i) {
    const int a = base + i * 4;
    const int b = base + ((i + 1) % rows) * 4;
    for (int k = 0; k < 3; ++k) {
      indices.insert(indices.end(), {a + k, b + k, b + k + 1, a + k, b + k + 1, a + k + 1});
    }
  }
}

} // namespace

SpriteLineSet::SpriteLineSet(std::vector<SpriteLines> lines) {
  for (SpriteLines &entry : lines) {
    Key key{entry.image, entry.rect};
    lines_.insert_or_assign(std::move(key), std::move(entry));
  }
}

const SpriteLines *SpriteLineSet::find(const Sprite &sprite) const {
  const auto found = lines_.find({sprite.image, sprite.rect});
  return found == lines_.end() ? nullptr : &found->second;
}

void draw_sprite_lines(SDL_Renderer *renderer, const SpriteLines &lines,
                       const LinePlacement &placement, const std::uint32_t tint,
                       const float alpha) {
  const float width = lines.rect[2] - lines.rect[0];
  const float height = lines.rect[3] - lines.rect[1];
  if (width <= 0.0F || height <= 0.0F || placement.target.w < 0.5F) {
    return;
  }
  const float scale_x = placement.target.w / width;
  const float scale_y = placement.target.h / height;
  std::vector<SDL_Vertex> vertices;
  std::vector<int> indices;
  for (const LineStroke &line : lines.strokes) {
    const auto channel = [&](const int shift) {
      return static_cast<float>((line.colour >> shift) & 0xffU) *
             static_cast<float>((tint >> shift) & 0xffU) / (255.0F * 255.0F);
    };
    const SDL_FColor colour{channel(16), channel(8), channel(0), line.alpha * alpha};
    const float half = 0.25F * line.width * (scale_x + scale_y);
    for (const LinePolyline &path : line.paths) {
      stroke(to_pixels(path, placement, scale_x, scale_y), path.closed, half, colour, vertices,
             indices);
    }
  }
  if (indices.empty()) {
    return;
  }
  SDL_BlendMode previous = SDL_BLENDMODE_NONE;
  SDL_GetRenderDrawBlendMode(renderer, &previous);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()),
                     indices.data(), static_cast<int>(indices.size()));
  SDL_SetRenderDrawBlendMode(renderer, previous);
}

bool draw_clean_art(TextureCache &textures, const Sprite &sprite,
                    const std::vector<EraseArea> &areas, const Viewport &viewport,
                    const float alpha, const SDL_FRect &target) {
  const SpriteLineSet *set = textures.sprite_lines();
  const SpriteLines *lines = set != nullptr ? set->find(sprite) : nullptr;
  SDL_Texture *art = textures.erased(sprite.image, sprite.rect, areas, lines);
  if (art == nullptr) {
    return false;
  }
  SDL_SetTextureColorMod(art, static_cast<Uint8>(sprite.colour >> 16U),
                         static_cast<Uint8>(sprite.colour >> 8U),
                         static_cast<Uint8>(sprite.colour));
  SDL_SetTextureAlphaMod(art, static_cast<Uint8>(std::clamp(alpha, 0.0F, 1.0F) * 255 + 0.5F));
  // The erased piece is its own texture: its rect starts at the origin.
  Sprite piece = sprite;
  const float width = sprite.rect[2] - sprite.rect[0];
  const float height = sprite.rect[3] - sprite.rect[1];
  piece.rect = {0.0F, 0.0F, width, height};
  SDL_FRect source{0.0F, 0.0F, width, height};
  SDL_FRect area = target;
  LinePlacement placement{target, std::nullopt, std::nullopt};
  if (sprite.angle == 0.0F && !sprite.flip_x) {
    draw_sprite_edges(textures, art, piece, viewport, source, area);
    if (sprite.extend_left) {
      placement.reach_left = viewport.origin_x;
    }
    if (sprite.extend_right) {
      placement.reach_right = viewport.origin_x + static_cast<float>(viewport.pixel_width);
    }
  }
  record_quad(textures, art, source, area);
  SDL_RenderTexture(textures.renderer(), art, &source, &area);
  if (lines != nullptr) {
    draw_sprite_lines(textures.renderer(), *lines, placement, sprite.colour, alpha);
  }
  return true;
}

std::unique_ptr<SpriteLineSet> load_sprite_lines(const DataRoots &roots) {
  try {
    return std::make_unique<SpriteLineSet>(
        parse_sprite_lines(roots.document("ui/sprite_lines.json")));
  } catch (const std::exception &error) {
    SDL_Log("ui/sprite_lines.json: %s", error.what());
    return nullptr;
  }
}

} // namespace phantomcrash
