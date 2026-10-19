#include "phantomcrash/skeleton.hpp"
#include <cmath>

namespace phantomcrash {
Mat4 rotation_x(const float angle) {
  Mat4 m;
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  m.at(1, 1) = c;
  m.at(1, 2) = s;
  m.at(2, 1) = -s;
  m.at(2, 2) = c;
  return m;
}

Mat4 rotation_y(const float angle) {
  Mat4 m;
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  m.at(0, 0) = c;
  m.at(0, 2) = -s;
  m.at(2, 0) = s;
  m.at(2, 2) = c;
  return m;
}

Mat4 rotation_z(const float angle) {
  Mat4 m;
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  m.at(0, 0) = c;
  m.at(0, 1) = s;
  m.at(1, 0) = -s;
  m.at(1, 1) = c;
  return m;
}
} // namespace phantomcrash
