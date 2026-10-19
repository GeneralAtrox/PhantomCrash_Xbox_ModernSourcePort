#include "phantomcrash/sprite_lines.hpp"

#include "phantomcrash/sprite_layer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace phantomcrash {

namespace {

using Point = std::array<float, 2>;

Point lerp(const Point &a, const Point &b, const float t) {
  return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t};
}

float distance_to_chord(const Point &p, const Point &a, const Point &b) {
  const float dx = b[0] - a[0];
  const float dy = b[1] - a[1];
  const float length = std::hypot(dx, dy);
  if (length < 1e-6F) {
    return std::hypot(p[0] - a[0], p[1] - a[1]);
  }
  return std::abs((p[0] - a[0]) * dy - (p[1] - a[1]) * dx) / length;
}

// A cubic split in half until its controls lie within `tolerance` of its
// chord; each piece's end appended.
void flatten_cubic(const Point &p0, const Point &p1, const Point &p2, const Point &p3,
                   const float tolerance, std::vector<Point> &out, const int depth = 0) {
  if (depth >= 16 ||
      std::max(distance_to_chord(p1, p0, p3), distance_to_chord(p2, p0, p3)) <= tolerance) {
    out.push_back(p3);
    return;
  }
  const Point a = lerp(p0, p1, 0.5F), b = lerp(p1, p2, 0.5F), c = lerp(p2, p3, 0.5F);
  const Point ab = lerp(a, b, 0.5F), bc = lerp(b, c, 0.5F);
  const Point middle = lerp(ab, bc, 0.5F);
  flatten_cubic(p0, a, ab, middle, tolerance, out, depth + 1);
  flatten_cubic(middle, bc, c, p3, tolerance, out, depth + 1);
}

// Reads the path data's commands and numbers in turn.
class PathReader {
public:
  explicit PathReader(const std::string_view data) : data_(data) {}

  // The next command letter, or the last one again before more numbers
  // (an implicit repeat); 0 at the end.
  char command(const char last) {
    skip();
    if (at_ >= data_.size()) {
      return 0;
    }
    const char c = data_[at_];
    if (std::isalpha(static_cast<unsigned char>(c)) != 0) {
      ++at_;
      return c;
    }
    if (last == 0) {
      throw std::runtime_error("line path must start with a command");
    }
    return last;
  }

  float number() {
    skip();
    const std::string rest(data_.substr(at_, 32));
    char *end = nullptr;
    const float value = std::strtof(rest.c_str(), &end);
    if (end == rest.c_str()) {
      throw std::runtime_error("line path expects a number");
    }
    at_ += static_cast<std::size_t>(end - rest.c_str());
    return value;
  }

private:
  void skip() {
    while (at_ < data_.size() &&
           (std::isspace(static_cast<unsigned char>(data_[at_])) != 0 || data_[at_] == ',')) {
      ++at_;
    }
  }

  std::string_view data_;
  std::size_t at_{};
};

} // namespace

std::vector<LinePolyline> parse_line_path(const std::string_view data, const float tolerance) {
  std::vector<LinePolyline> paths;
  PathReader reader(data);
  Point current{}, start{}, control{};
  char last = 0;
  char previous = 0; // the command before, for S and T's reflected control
  const auto point = [&](const bool relative) {
    const float x = reader.number();
    const float y = reader.number();
    return relative ? Point{current[0] + x, current[1] + y} : Point{x, y};
  };
  const auto add = [&](const Point &p) {
    if (paths.empty()) {
      throw std::runtime_error("line path must start with M");
    }
    paths.back().points.push_back(p);
    current = p;
  };
  while (const char c = reader.command(last)) {
    const bool relative = std::islower(static_cast<unsigned char>(c)) != 0;
    const char op = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    switch (op) {
    case 'M':
      current = point(relative);
      start = current;
      paths.push_back({{current}, false});
      last = relative ? 'l' : 'L'; // numbers after M are lines
      previous = op;
      continue;
    case 'L':
      add(point(relative));
      break;
    case 'H': {
      const float x = reader.number();
      add({relative ? current[0] + x : x, current[1]});
      break;
    }
    case 'V': {
      const float y = reader.number();
      add({current[0], relative ? current[1] + y : y});
      break;
    }
    case 'C':
    case 'S': {
      const Point from = current;
      const Point first = op == 'C' ? point(relative)
                          : previous == 'C' || previous == 'S'
                              ? Point{2 * from[0] - control[0], 2 * from[1] - control[1]}
                              : from;
      control = point(relative);
      const Point to = point(relative);
      std::vector<Point> flat;
      flatten_cubic(from, first, control, to, tolerance, flat);
      for (const Point &p : flat) {
        add(p);
      }
      break;
    }
    case 'Q':
    case 'T': {
      const Point from = current;
      control = op == 'Q' ? point(relative)
                : previous == 'Q' || previous == 'T'
                    ? Point{2 * from[0] - control[0], 2 * from[1] - control[1]}
                    : from;
      const Point to = point(relative);
      std::vector<Point> flat;
      flatten_cubic(from, lerp(from, control, 2.0F / 3.0F), lerp(to, control, 2.0F / 3.0F), to,
                    tolerance, flat);
      for (const Point &p : flat) {
        add(p);
      }
      break;
    }
    case 'Z':
      if (paths.empty()) {
        throw std::runtime_error("line path must start with M");
      }
      paths.back().closed = true;
      current = start;
      break;
    default:
      throw std::runtime_error(std::string("line path command not supported: ") + c);
    }
    last = op == 'Z' ? 0 : c;
    previous = op;
  }
  std::erase_if(paths, [](const LinePolyline &path) { return path.points.size() < 2; });
  return paths;
}

std::vector<SpriteLines> parse_sprite_lines(const json::JsonValue &document) {
  std::vector<SpriteLines> out;
  const auto *list = document.find("lines");
  if (list == nullptr) {
    return out;
  }
  for (const auto &entry : list->array()) {
    SpriteLines lines;
    lines.image = entry.string_or("image", "");
    const auto *rect = entry.find("rect");
    const auto *strokes = entry.find("strokes");
    if (lines.image.empty() || rect == nullptr || strokes == nullptr || rect->array().size() != 4) {
      throw std::runtime_error("sprite lines need an image, rect and strokes");
    }
    for (std::size_t i = 0; i < 4; ++i) {
      lines.rect[i] = static_cast<float>(rect->array()[i].number());
    }
    for (const auto &value : strokes->array()) {
      LineStroke stroke;
      stroke.width = static_cast<float>(value.number_or("width", 1.0));
      stroke.colour = parse_colour(value.string_or("colour", "#ffffff"));
      stroke.alpha = std::clamp(static_cast<float>(value.number_or("alpha", 1.0)), 0.0F, 1.0F);
      stroke.erase = static_cast<float>(
          value.number_or("erase", 0.5 * static_cast<double>(stroke.width) + 1.0));
      const auto *paths = value.find("paths");
      if (paths == nullptr || stroke.width <= 0.0F) {
        throw std::runtime_error("a sprite line stroke needs paths and a width above 0");
      }
      for (const auto &path : paths->array()) {
        for (auto &polyline : parse_line_path(path.string())) {
          stroke.paths.push_back(std::move(polyline));
        }
      }
      lines.strokes.push_back(std::move(stroke));
    }
    out.push_back(std::move(lines));
  }
  return out;
}

} // namespace phantomcrash
