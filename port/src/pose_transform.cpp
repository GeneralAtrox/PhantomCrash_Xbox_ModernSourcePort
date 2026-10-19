#include "phantomcrash/weapon_mounts.hpp"
#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {
using Quaternion = std::array<float, 4>;
Quaternion quaternion(const Mat4 &m) {
  Quaternion q{};
  const float trace = m.at(0, 0) + m.at(1, 1) + m.at(2, 2);
  if (trace > 0) {
    const float s = std::sqrt(trace + 1.0F) * 2;
    q = {(m.at(1, 2) - m.at(2, 1)) / s, (m.at(2, 0) - m.at(0, 2)) / s,
         (m.at(0, 1) - m.at(1, 0)) / s, s * 0.25F};
  } else {
    int i = 0;
    if (m.at(1, 1) > m.at(i, i)) {
      i = 1;
    }
    if (m.at(2, 2) > m.at(i, i)) {
      i = 2;
    }
    const int j = (i + 1) % 3, k = (i + 2) % 3;
    const float s = std::sqrt(std::max(0.0F, 1 + m.at(i, i) - m.at(j, j) - m.at(k, k))) * 2;
    if (s < 1e-6F) {
      return {0, 0, 0, 1};
    }
    q[i] = s * 0.25F;
    q[j] = (m.at(i, j) + m.at(j, i)) / s;
    q[k] = (m.at(i, k) + m.at(k, i)) / s;
    q[3] = (m.at(j, k) - m.at(k, j)) / s;
  }
  return q;
}
} // namespace

Mat4 blend_pose_transform(const Mat4 &from, const Mat4 &to, const float weight) {
  const float t = std::clamp(weight, 0.0F, 1.0F);
  if (t == 0) {
    return from;
  }
  if (t == 1) {
    return to;
  }
  auto a = quaternion(from), b = quaternion(to);
  float cosine = 0;
  for (int i = 0; i < 4; ++i) {
    cosine += a[i] * b[i];
  }
  if (cosine < 0) {
    for (float &v : b) {
      v = -v;
    }
    cosine = -cosine;
  }
  float wa = 1 - t, wb = t;
  if (cosine < 0.9995F) {
    const float angle = std::acos(std::clamp(cosine, -1.0F, 1.0F));
    wa = std::sin((1 - t) * angle) / std::sin(angle);
    wb = std::sin(t * angle) / std::sin(angle);
  }
  float norm = 0;
  for (int i = 0; i < 4; ++i) {
    a[i] = wa * a[i] + wb * b[i];
    norm += a[i] * a[i];
  }
  for (float &v : a) {
    v /= std::sqrt(norm);
  }
  Mat4 result = rotation(a[0], a[1], a[2], a[3]);
  for (int c = 0; c < 3; ++c) {
    result.at(3, c) = from.at(3, c) * (1 - t) + to.at(3, c) * t;
  }
  return result;
}

Mat4 aim_pose_transform(const Mat4 &pose, const Vec3 target, const Vec3 up) {
  const Vec3 origin = transform_point({}, pose);
  const Vec3 z = normalize(origin - target); // the weapon points down local -Z
  if (length(z) < 1e-5F) {
    return pose;
  }
  Vec3 x = normalize(cross(up, z));
  if (length(x) < 1e-5F) {
    x = normalize(cross(Vec3{1, 0, 0}, z));
  }
  const Vec3 axes[] = {x, cross(z, x), z, origin};
  Mat4 result;
  for (int row = 0; row < 4; ++row) {
    result.at(row, 0) = axes[row].x;
    result.at(row, 1) = axes[row].y;
    result.at(row, 2) = axes[row].z;
  }
  return result;
}
} // namespace phantomcrash
