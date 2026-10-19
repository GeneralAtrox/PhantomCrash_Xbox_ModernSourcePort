#pragma once

#include <array>
#include <cmath>

namespace phantomcrash {

// 3D maths in the original's convention: left-handed world, Y up, row
// vectors (v' = v * M), so a chain of transforms reads left to right, as
// with D3DX. Matrices are row-major; HLSL constant buffers take them
// transposed.
struct Vec3 {
  float x{}, y{}, z{};
};

[[nodiscard]] inline Vec3 operator+(const Vec3 a, const Vec3 b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
[[nodiscard]] inline Vec3 operator-(const Vec3 a, const Vec3 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
[[nodiscard]] inline Vec3 operator*(const Vec3 a, const float s) {
  return {a.x * s, a.y * s, a.z * s};
}
[[nodiscard]] inline float dot(const Vec3 a, const Vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
[[nodiscard]] inline Vec3 cross(const Vec3 a, const Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] inline float length(const Vec3 a) {
  return std::sqrt(dot(a, a));
}
[[nodiscard]] inline Vec3 normalize(const Vec3 a) {
  const float l = length(a);
  return l > 0.0F ? a * (1.0F / l) : Vec3{};
}

struct Mat4 {
  std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

  [[nodiscard]] float at(const int row, const int column) const {
    return m[static_cast<std::size_t>(row * 4 + column)];
  }
  float &at(const int row, const int column) {
    return m[static_cast<std::size_t>(row * 4 + column)];
  }
};

[[nodiscard]] inline Mat4 operator*(const Mat4 &a, const Mat4 &b) {
  Mat4 out;
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      float sum = 0.0F;
      for (int k = 0; k < 4; ++k) {
        sum += a.at(row, k) * b.at(k, column);
      }
      out.at(row, column) = sum;
    }
  }
  return out;
}

[[nodiscard]] inline Vec3 transform_point(const Vec3 p, const Mat4 &m) {
  return {p.x * m.at(0, 0) + p.y * m.at(1, 0) + p.z * m.at(2, 0) + m.at(3, 0),
          p.x * m.at(0, 1) + p.y * m.at(1, 1) + p.z * m.at(2, 1) + m.at(3, 1),
          p.x * m.at(0, 2) + p.y * m.at(1, 2) + p.z * m.at(2, 2) + m.at(3, 2)};
}

[[nodiscard]] inline Mat4 transposed(const Mat4 &a) {
  Mat4 out;
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      out.at(row, column) = a.at(column, row);
    }
  }
  return out;
}

[[nodiscard]] inline Mat4 translation(const Vec3 t) {
  Mat4 out;
  out.at(3, 0) = t.x;
  out.at(3, 1) = t.y;
  out.at(3, 2) = t.z;
  return out;
}

[[nodiscard]] inline Mat4 scaling(const Vec3 s) {
  Mat4 out;
  out.at(0, 0) = s.x;
  out.at(1, 1) = s.y;
  out.at(2, 2) = s.z;
  return out;
}

// Rotation matrix of a unit quaternion (x, y, z, w) about the origin, for
// row vectors in the same handedness the quaternion was written in.
[[nodiscard]] inline Mat4 rotation(const float x, const float y, const float z, const float w) {
  Mat4 out;
  out.at(0, 0) = 1 - 2 * (y * y + z * z);
  out.at(0, 1) = 2 * (x * y + z * w);
  out.at(0, 2) = 2 * (x * z - y * w);
  out.at(1, 0) = 2 * (x * y - z * w);
  out.at(1, 1) = 1 - 2 * (x * x + z * z);
  out.at(1, 2) = 2 * (y * z + x * w);
  out.at(2, 0) = 2 * (x * z + y * w);
  out.at(2, 1) = 2 * (y * z - x * w);
  out.at(2, 2) = 1 - 2 * (x * x + y * y);
  return out;
}

// D3DXMatrixLookAtLH.
[[nodiscard]] inline Mat4 look_at(const Vec3 eye, const Vec3 target, const Vec3 up) {
  const Vec3 z = normalize(target - eye);
  const Vec3 x = normalize(cross(up, z));
  const Vec3 y = cross(z, x);
  Mat4 out;
  out.at(0, 0) = x.x;
  out.at(1, 0) = x.y;
  out.at(2, 0) = x.z;
  out.at(0, 1) = y.x;
  out.at(1, 1) = y.y;
  out.at(2, 1) = y.z;
  out.at(0, 2) = z.x;
  out.at(1, 2) = z.y;
  out.at(2, 2) = z.z;
  out.at(3, 0) = -dot(x, eye);
  out.at(3, 1) = -dot(y, eye);
  out.at(3, 2) = -dot(z, eye);
  return out;
}

// D3DXMatrixPerspectiveFovLH (depth 0..1). The vertical field of view is
// fixed, so a wider window sees more to the sides (Hor+).
[[nodiscard]] inline Mat4 perspective(const float vertical_fov_radians, const float aspect,
                                      const float near_plane, const float far_plane) {
  const float y_scale = 1.0F / std::tan(vertical_fov_radians * 0.5F);
  Mat4 out;
  out.m.fill(0.0F);
  out.at(0, 0) = y_scale / aspect;
  out.at(1, 1) = y_scale;
  out.at(2, 2) = far_plane / (far_plane - near_plane);
  out.at(2, 3) = 1.0F;
  out.at(3, 2) = -near_plane * far_plane / (far_plane - near_plane);
  return out;
}

inline constexpr float pi = 3.14159265358979323846F;

[[nodiscard]] inline float radians(const float degrees) {
  return degrees * pi / 180.0F;
}

} // namespace phantomcrash
