#include "desktop_sprite_shapes.hpp"

#include <algorithm>
#include <cmath>
#include <exception>

namespace phantomcrash {

namespace {

// Corner segments: about one per two pixels of the largest radius.
constexpr float pixels_per_segment = 2.0F;
constexpr int max_segments = 24;

struct Mapping {
  float left{}, top{}, scale_x{}, scale_y{};
  [[nodiscard]] SDL_FPoint to_pixels(const std::array<float, 2> &point) const {
    return {left + point[0] * scale_x, top + point[1] * scale_y};
  }
};

// The stop's colour, blended `shade` of the way to its shade colour.
SDL_FColor stop_colour(const ShapeStop &stop, const std::uint32_t tint, const float alpha,
                       const float shade = 0.0F) {
  const auto channel = [&](const int shift) {
    const float lit = static_cast<float>((stop.colour >> shift) & 0xffU);
    const float dark = static_cast<float>((stop.shade_colour >> shift) & 0xffU);
    return (lit + (dark - lit) * shade) * static_cast<float>((tint >> shift) & 0xffU) /
           (255.0F * 255.0F);
  };
  return {channel(16), channel(8), channel(0),
          (stop.alpha + (stop.shade_alpha - stop.alpha) * shade) * alpha};
}

// How far each point of a contour faces `toward` (unit length): 0 facing
// away, 1 facing it, a half across it, from the outward normal through the
// point's neighbours.
std::vector<float> contour_shade(const std::vector<std::array<float, 2>> &contour,
                                 const std::array<float, 2> &toward) {
  const std::size_t count = contour.size();
  float area = 0.0F;
  for (std::size_t index = 0; index < count; ++index) {
    const auto &a = contour[index];
    const auto &b = contour[(index + 1) % count];
    area += a[0] * b[1] - b[0] * a[1];
  }
  const float outward = area >= 0.0F ? 1.0F : -1.0F;
  // An edge within this of facing straight at (or away from) the direction
  // takes the full shade (or none).
  constexpr float spread = 0.7F;
  std::vector<float> shades(count);
  for (std::size_t index = 0; index < count; ++index) {
    const auto &before = contour[(index + count - 1) % count];
    const auto &after = contour[(index + 1) % count];
    const float dx = after[0] - before[0], dy = after[1] - before[1];
    const float length = std::max(std::hypot(dx, dy), 1e-6F);
    const float facing = outward * (dy * toward[0] - dx * toward[1]) / length;
    shades[index] = std::clamp((facing + spread) / (2.0F * spread), 0.0F, 1.0F);
  }
  return shades;
}

SDL_FColor gradient_colour(const ShapeGradient &gradient, const float at, const std::uint32_t tint,
                           const float alpha) {
  const auto &stops = gradient.stops;
  GradientStop from = stops.front(), to = from;
  float t = 0.0F;
  if (at >= stops.back().at) {
    from = to = stops.back();
  } else if (at > stops.front().at) {
    const auto next = std::upper_bound(stops.begin(), stops.end(), at,
                                       [](const float x, const auto &stop) { return x < stop.at; });
    from = *(next - 1);
    to = *next;
    t = (at - from.at) / std::max(to.at - from.at, 1e-4F);
  }
  const auto channel = [&](const int shift) {
    const float a = static_cast<float>((from.colour >> shift) & 0xffU);
    const float b = static_cast<float>((to.colour >> shift) & 0xffU);
    return (a + (b - a) * t) * static_cast<float>((tint >> shift) & 0xffU) / (255.0F * 255.0F);
  };
  return {channel(16), channel(8), channel(0), alpha * (from.alpha + (to.alpha - from.alpha) * t)};
}

// The convex `contour` filled in slices across the gradient's axis, cut at
// every stop and every contour point, so each slice's colour is linear.
void fill_shaded(const std::vector<std::array<float, 2>> &contour, const ShapeGradient &gradient,
                 const Mapping &mapping, const std::uint32_t tint, const float alpha,
                 std::vector<SDL_Vertex> &vertices, std::vector<int> &indices) {
  const std::size_t along = gradient.vertical ? 1 : 0, across = 1 - along;
  std::vector<float> cuts;
  float low = contour.front()[along], high = low;
  for (const auto &point : contour) {
    cuts.push_back(point[along]);
    low = std::min(low, point[along]);
    high = std::max(high, point[along]);
  }
  for (const auto &stop : gradient.stops) {
    if (stop.at > low && stop.at < high) {
      cuts.push_back(stop.at);
    }
  }
  std::sort(cuts.begin(), cuts.end());
  cuts.erase(std::unique(cuts.begin(), cuts.end(),
                         [](const float a, const float b) { return b - a < 1e-4F; }),
             cuts.end());
  // The contour's extent across the axis where it crosses `at`.
  const auto span = [&](const float at) {
    std::array<float, 2> range{1e9F, -1e9F};
    const auto include = [&](const float x) {
      range = {std::min(range[0], x), std::max(range[1], x)};
    };
    for (std::size_t index = 0; index < contour.size(); ++index) {
      const auto &a = contour[index];
      const auto &b = contour[(index + 1) % contour.size()];
      const float lo = std::min(a[along], b[along]), hi = std::max(a[along], b[along]);
      if (at < lo - 1e-4F || at > hi + 1e-4F) {
        continue;
      }
      if (hi - lo < 1e-4F) {
        include(a[across]);
        include(b[across]);
      } else {
        const float t = std::clamp((at - a[along]) / (b[along] - a[along]), 0.0F, 1.0F);
        include(a[across] + (b[across] - a[across]) * t);
      }
    }
    return range;
  };
  const auto point = [&](const float at, const float side) {
    std::array<float, 2> p{};
    p[along] = at;
    p[across] = side;
    return mapping.to_pixels(p);
  };
  for (std::size_t index = 0; index + 1 < cuts.size(); ++index) {
    const float a = cuts[index], b = cuts[index + 1];
    const auto near = span(a), far = span(b);
    const SDL_FColor ca = gradient_colour(gradient, a, tint, alpha);
    const SDL_FColor cb = gradient_colour(gradient, b, tint, alpha);
    const int base = static_cast<int>(vertices.size());
    vertices.push_back({point(a, near[0]), ca, {}});
    vertices.push_back({point(a, near[1]), ca, {}});
    vertices.push_back({point(b, far[1]), cb, {}});
    vertices.push_back({point(b, far[0]), cb, {}});
    indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
  }
}

// The stops with each hard edge spread over `edge` texels around its
// distance, so it is antialiased over one screen pixel.
std::vector<ShapeStop> spread_edges(std::vector<ShapeStop> stops, const float edge) {
  for (std::size_t index = 0; index + 1 < stops.size(); ++index) {
    if (stops[index].distance == stops[index + 1].distance) {
      stops[index].distance -= edge * 0.5F;
      stops[index + 1].distance += edge * 0.5F;
    }
  }
  for (std::size_t index = 1; index < stops.size(); ++index) {
    stops[index].distance = std::max(stops[index].distance, stops[index - 1].distance);
  }
  return stops;
}

void draw_outline(SDL_Renderer *renderer, const ShapeOutline &outline,
                  const std::array<float, 4> &box, const Mapping &mapping, const std::uint32_t tint,
                  const float alpha) {
  const float texel = 0.5F * (mapping.scale_x + mapping.scale_y);
  const std::vector<ShapeStop> stops = spread_edges(outline.stops, 1.0F / texel);
  const float widest = outline.radius + std::max(0.0F, outline.radius_growth) *
                                            std::max(0.0F, stops.back().distance);
  const int segments =
      std::clamp(static_cast<int>(std::ceil(widest * texel / pixels_per_segment)), 1, max_segments);
  std::vector<SDL_Vertex> vertices;
  std::vector<int> indices;
  const auto ring = [&](const ShapeStop &stop) {
    const auto contour = shape_contour(outline, box, stop.distance, segments);
    if (!outline.shade_toward) {
      const SDL_FColor colour = stop_colour(stop, tint, alpha);
      for (const auto &point : contour) {
        vertices.push_back({mapping.to_pixels(point), colour, {}});
      }
      return;
    }
    const std::vector<float> shades = contour_shade(contour, *outline.shade_toward);
    for (std::size_t index = 0; index < contour.size(); ++index) {
      vertices.push_back(
          {mapping.to_pixels(contour[index]), stop_colour(stop, tint, alpha, shades[index]), {}});
    }
  };
  // The first stop's colour fills the inside, as a fan from the centre,
  // or the fill's gradient does.
  const auto inside = shape_contour(outline, box, stops.front().distance, segments);
  const int count = static_cast<int>(inside.size());
  if (outline.fill) {
    fill_shaded(inside, *outline.fill, mapping, tint, stops.front().alpha * alpha, vertices,
                indices);
  } else if (stops.front().alpha > 0.0F) {
    std::array<float, 2> centre{(box[0] + box[2]) * 0.5F, (box[1] + box[3]) * 0.5F};
    if (!outline.path.empty()) {
      centre = {0.0F, 0.0F};
      for (const auto &[x, y] : inside) {
        centre = {centre[0] + x / static_cast<float>(count),
                  centre[1] + y / static_cast<float>(count)};
      }
    }
    vertices.push_back({mapping.to_pixels(centre), stop_colour(stops.front(), tint, alpha), {}});
    for (int point = 0; point < count; ++point) {
      indices.insert(indices.end(), {0, 1 + point, 1 + (point + 1) % count});
    }
  }
  const int first = static_cast<int>(vertices.size());
  for (const ShapeStop &stop : stops) {
    ring(stop);
  }
  for (std::size_t band = 0; band + 1 < stops.size(); ++band) {
    if (stops[band].alpha <= 0.0F && stops[band + 1].alpha <= 0.0F) {
      continue;
    }
    const int inner = first + static_cast<int>(band) * count;
    const int outer = inner + count;
    for (int point = 0; point < count; ++point) {
      const int next = (point + 1) % count;
      indices.insert(indices.end(), {inner + point, outer + point, outer + next, inner + point,
                                     outer + next, inner + next});
    }
  }
  if (!indices.empty()) {
    SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()),
                       indices.data(), static_cast<int>(indices.size()));
  }
}

} // namespace

SpriteShapes::SpriteShapes(std::vector<SpriteShape> shapes) {
  for (SpriteShape &shape : shapes) {
    Key key{shape.image, shape.rect};
    shapes_.insert_or_assign(std::move(key), std::move(shape));
  }
}

const SpriteShape *SpriteShapes::find(const Sprite &sprite) const {
  const auto found = shapes_.find({sprite.image, sprite.rect});
  return found == shapes_.end() ? nullptr : &found->second;
}

void draw_sprite_shape(SDL_Renderer *renderer, const SpriteShape &shape, const SDL_FRect &target,
                       const std::uint32_t tint, const float alpha) {
  const float width = shape.rect[2] - shape.rect[0];
  const float height = shape.rect[3] - shape.rect[1];
  if (width <= 0.0F || height <= 0.0F || target.w < 0.5F || target.h < 0.5F) {
    return;
  }
  const Mapping mapping{target.x, target.y, target.w / width, target.h / height};
  SDL_BlendMode previous = SDL_BLENDMODE_NONE;
  SDL_GetRenderDrawBlendMode(renderer, &previous);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  for (const ShapeOutline &outline : shape.outlines) {
    for (int row = 0; row < outline.count[1]; ++row) {
      for (int column = 0; column < outline.count[0]; ++column) {
        const float x = outline.step[0] * static_cast<float>(column);
        const float y = outline.step[1] * static_cast<float>(row);
        const std::array<float, 4> box{outline.box[0] + x, outline.box[1] + y, outline.box[2] + x,
                                       outline.box[3] + y};
        draw_outline(renderer, outline, box, mapping, tint, alpha);
      }
    }
  }
  SDL_SetRenderDrawBlendMode(renderer, previous);
}

std::unique_ptr<SpriteShapes> load_sprite_shapes(const DataRoots &roots) {
  try {
    return std::make_unique<SpriteShapes>(
        parse_sprite_shapes(roots.document("ui/sprite_shapes.json")));
  } catch (const std::exception &error) {
    SDL_Log("ui/sprite_shapes.json: %s", error.what());
    return nullptr;
  }
}

} // namespace phantomcrash
