#include "phantomcrash/sprite_shapes.hpp"

#include "phantomcrash/json_classes.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/sprite_lines.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace phantomcrash {

namespace {

template <std::size_t Size> std::array<float, Size> numbers(const json::JsonValue &value) {
  const auto &list = value.array();
  if (list.size() != Size) {
    throw std::runtime_error("sprite shape list needs " + std::to_string(Size) + " numbers");
  }
  std::array<float, Size> out{};
  for (std::size_t index = 0; index < Size; ++index) {
    out[index] = static_cast<float>(list[index].number());
  }
  return out;
}

// [distance, "#rrggbb", alpha, "#rrggbb", alpha]: alpha defaults to
// opaque, and the shade (the last two) to the colour and alpha.
ShapeStop parse_stop(const json::JsonValue &value) {
  const auto &list = value.array();
  if (list.size() < 2 || list.size() > 5 || list.size() == 4) {
    throw std::runtime_error("sprite shape stop is [distance, colour, alpha, shade, shade alpha]");
  }
  const auto alpha = [&](const std::size_t index) {
    return std::clamp(static_cast<float>(list[index].number()), 0.0F, 1.0F);
  };
  ShapeStop stop;
  stop.distance = static_cast<float>(list[0].number());
  stop.colour = parse_colour(list[1].string());
  stop.alpha = list.size() >= 3 ? alpha(2) : 1.0F;
  stop.shade_colour = list.size() == 5 ? parse_colour(list[3].string()) : stop.colour;
  stop.shade_alpha = list.size() == 5 ? alpha(4) : stop.alpha;
  return stop;
}

// {"axis": "x" or "y", "stops": [[position, "#rrggbb", alpha?], ...]}.
ShapeGradient parse_gradient(const json::JsonValue &value) {
  ShapeGradient gradient;
  const std::string axis = value.string_or("axis", "y");
  if (axis != "x" && axis != "y") {
    throw std::runtime_error("sprite shape fill axis is x or y");
  }
  gradient.vertical = axis == "y";
  const auto *stops = value.find("stops");
  if (stops == nullptr || stops->array().empty()) {
    throw std::runtime_error("sprite shape fill needs stops");
  }
  for (const auto &stop : stops->array()) {
    const auto &pair = stop.array();
    if (pair.size() != 2 && pair.size() != 3) {
      throw std::runtime_error("sprite shape fill stop is [position, colour, alpha?]");
    }
    GradientStop parsed{static_cast<float>(pair[0].number()), parse_colour(pair[1].string())};
    if (pair.size() == 3) {
      parsed.alpha = std::clamp(static_cast<float>(pair[2].number()), 0.0F, 1.0F);
    }
    gradient.stops.push_back(parsed);
  }
  std::stable_sort(gradient.stops.begin(), gradient.stops.end(),
                   [](const auto &a, const auto &b) { return a.at < b.at; });
  return gradient;
}

// The first closed contour of SVG path data, moved by `offset`, without a
// repeated closing point, and its bounds.
void parse_path(const json::JsonValue &value, ShapeOutline &outline) {
  std::array<float, 2> offset{};
  if (const auto *by = value.find("offset")) {
    offset = numbers<2>(*by);
  }
  const auto polylines = parse_line_path(value.find("path")->string());
  if (polylines.empty() || polylines.front().points.size() < 3) {
    throw std::runtime_error("sprite shape path needs a closed contour of 3 points or more");
  }
  outline.box = {1e9F, 1e9F, -1e9F, -1e9F};
  for (const auto &[x, y] : polylines.front().points) {
    const std::array<float, 2> point{x + offset[0], y + offset[1]};
    if (!outline.path.empty() && std::abs(point[0] - outline.path.back()[0]) < 1e-4F &&
        std::abs(point[1] - outline.path.back()[1]) < 1e-4F) {
      continue;
    }
    outline.path.push_back(point);
    outline.box = {std::min(outline.box[0], point[0]), std::min(outline.box[1], point[1]),
                   std::max(outline.box[2], point[0]), std::max(outline.box[3], point[1])};
  }
  const auto &first = outline.path.front();
  if (std::abs(first[0] - outline.path.back()[0]) < 1e-4F &&
      std::abs(first[1] - outline.path.back()[1]) < 1e-4F) {
    outline.path.pop_back();
  }
}

// `path` moved `distance` outward (inward when negative): each point along
// the bisector of its two edges' outward normals, as far as keeps both
// edges `distance` away (at most twice it, at a spike).
std::vector<std::array<float, 2>> offset_path(const std::vector<std::array<float, 2>> &path,
                                              const std::array<float, 2> &shift,
                                              const float distance) {
  const std::size_t count = path.size();
  float area = 0.0F;
  for (std::size_t index = 0; index < count; ++index) {
    const auto &a = path[index];
    const auto &b = path[(index + 1) % count];
    area += a[0] * b[1] - b[0] * a[1];
  }
  const float outward = area >= 0.0F ? 1.0F : -1.0F; // y runs down the screen
  const auto normal = [&](const std::array<float, 2> &a, const std::array<float, 2> &b) {
    const float dx = b[0] - a[0], dy = b[1] - a[1];
    const float length = std::max(std::hypot(dx, dy), 1e-6F);
    return std::array<float, 2>{outward * dy / length, -outward * dx / length};
  };
  std::vector<std::array<float, 2>> points;
  points.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const auto &before = path[(index + count - 1) % count];
    const auto &point = path[index];
    const auto &after = path[(index + 1) % count];
    const auto n1 = normal(before, point), n2 = normal(point, after);
    std::array<float, 2> bisector{n1[0] + n2[0], n1[1] + n2[1]};
    const float length = std::hypot(bisector[0], bisector[1]);
    if (length < 1e-4F) {
      bisector = n1;
    } else {
      bisector = {bisector[0] / length, bisector[1] / length};
    }
    const float reach = distance / std::max(bisector[0] * n1[0] + bisector[1] * n1[1], 0.5F);
    points.push_back(
        {point[0] + shift[0] + bisector[0] * reach, point[1] + shift[1] + bisector[1] * reach});
  }
  return points;
}

ShapeOutline parse_outline(const json::JsonValue &value) {
  ShapeOutline outline;
  const auto *box = value.find("box");
  const auto *stops = value.find("stops");
  if (value.find("path") != nullptr && stops != nullptr) {
    parse_path(value, outline);
  } else if (box == nullptr || stops == nullptr) {
    throw std::runtime_error("sprite shape outline needs a box or path, and stops");
  } else {
    outline.box = numbers<4>(*box);
  }
  outline.radius = std::max(0.0F, static_cast<float>(value.number_or("radius", 0.0)));
  outline.radius_growth = static_cast<float>(value.number_or("radius_growth", 1.0));
  for (const auto &stop : stops->array()) {
    outline.stops.push_back(parse_stop(stop));
  }
  if (outline.stops.empty()) {
    throw std::runtime_error("sprite shape outline needs stops");
  }
  std::stable_sort(outline.stops.begin(), outline.stops.end(),
                   [](const ShapeStop &a, const ShapeStop &b) { return a.distance < b.distance; });
  if (const auto *fill = value.find("fill")) {
    outline.fill = parse_gradient(*fill);
  }
  if (const auto *toward = value.find("shade_toward")) {
    const auto direction = numbers<2>(*toward);
    const float length = std::hypot(direction[0], direction[1]);
    if (length <= 0.0F) {
      throw std::runtime_error("sprite shape shade_toward needs a direction");
    }
    outline.shade_toward = std::array<float, 2>{direction[0] / length, direction[1] / length};
  }
  if (const auto *count = value.find("count")) {
    const auto cells = numbers<2>(*count);
    outline.count = {std::max(1, static_cast<int>(cells[0])),
                     std::max(1, static_cast<int>(cells[1]))};
  }
  if (const auto *step = value.find("step")) {
    outline.step = numbers<2>(*step);
  }
  return outline;
}

} // namespace

std::vector<ShapeOutline> parse_shape_outlines(const json::JsonValue &list) {
  std::vector<ShapeOutline> outlines;
  for (const auto &outline : list.array()) {
    outlines.push_back(parse_outline(outline));
  }
  if (outlines.empty()) {
    throw std::runtime_error("shape needs at least one outline");
  }
  return outlines;
}

std::vector<SpriteShape> parse_sprite_shapes(const json::JsonValue &document) {
  std::vector<SpriteShape> shapes;
  const auto *list = document.find("shapes");
  if (list == nullptr) {
    return shapes;
  }
  const auto *classes = document.find("classes");
  for (const auto &listed : list->array()) {
    const json::JsonValue entry = json::with_classes(listed, classes);
    SpriteShape shape;
    shape.image = entry.string_or("image", "");
    const auto *rect = entry.find("rect");
    const auto *outlines = entry.find("outlines");
    if (shape.image.empty() || rect == nullptr || outlines == nullptr) {
      throw std::runtime_error("sprite shape needs an image, rect and outlines");
    }
    shape.rect = numbers<4>(*rect);
    for (const auto &outline : outlines->array()) {
      shape.outlines.push_back(parse_outline(json::with_classes(outline, classes)));
    }
    shapes.push_back(std::move(shape));
  }
  return shapes;
}

std::vector<std::array<float, 2>> shape_contour(const ShapeOutline &outline,
                                                const std::array<float, 4> &box,
                                                const float distance, const int segments) {
  if (!outline.path.empty()) {
    return offset_path(outline.path, {box[0] - outline.box[0], box[1] - outline.box[1]}, distance);
  }
  // The edge of a rounded rectangle moved by `distance` is one with its
  // radius grown by as much (with a growth of 1), square once the radius is
  // used up.
  const float centre_x = (box[0] + box[2]) * 0.5F;
  const float centre_y = (box[1] + box[3]) * 0.5F;
  const float half_x = std::max(0.0F, (box[2] - box[0]) * 0.5F + distance);
  const float half_y = std::max(0.0F, (box[3] - box[1]) * 0.5F + distance);
  const float round =
      std::clamp(outline.radius + outline.radius_growth * distance, 0.0F, std::min(half_x, half_y));
  // Corner centres clockwise from the top left, with each arc's start angle.
  const std::array<std::array<float, 3>, 4> corners{{
      {-1.0F, -1.0F, std::numbers::pi_v<float>},
      {1.0F, -1.0F, std::numbers::pi_v<float> * 1.5F},
      {1.0F, 1.0F, 0.0F},
      {-1.0F, 1.0F, std::numbers::pi_v<float> * 0.5F},
  }};
  const int steps = std::max(1, segments);
  std::vector<std::array<float, 2>> points;
  points.reserve(static_cast<std::size_t>(4 * (steps + 1)));
  for (const auto &[side_x, side_y, start] : corners) {
    const float corner_x = centre_x + side_x * (half_x - round);
    const float corner_y = centre_y + side_y * (half_y - round);
    for (int step = 0; step <= steps; ++step) {
      const float angle = start + std::numbers::pi_v<float> * 0.5F * static_cast<float>(step) /
                                      static_cast<float>(steps);
      points.push_back({corner_x + round * std::cos(angle), corner_y + round * std::sin(angle)});
    }
  }
  return points;
}

} // namespace phantomcrash
