#include "phantomcrash/source_pmt.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace phantomcrash {
namespace {
template <std::size_t N> std::array<float, N> vector(const json::JsonValue &value) {
  if (!value.is_array() || value.array().size() != N) {
    throw std::runtime_error("Invalid source PMT vector");
  }
  std::array<float, N> result;
  for (std::size_t i = 0; i < N; ++i) {
    result[i] = static_cast<float>(value.array()[i].number());
    if (!std::isfinite(result[i])) {
      throw std::runtime_error("Nonfinite source PMT vector");
    }
  }
  return result;
}

std::array<float, 4> unit(std::array<float, 4> q) {
  float length = 0;
  for (const float x : q) {
    length += x * x;
  }
  if (length < 1e-12F) {
    throw std::runtime_error("Zero source PMT quaternion");
  }
  for (float &x : q) {
    x /= std::sqrt(length);
  }
  return q;
}

std::array<float, 4> interpolate(std::array<float, 4> a, std::array<float, 4> b, const float t) {
  a = unit(a);
  b = unit(b);
  float dot = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    dot += a[i] * b[i];
  }
  if (dot < 0) {
    dot = -dot;
    for (float &x : b) {
      x = -x;
    }
  }
  float wa = 1 - t, wb = t;
  if (dot < .9995F) {
    const float angle = std::acos(std::clamp(dot, -1.0F, 1.0F));
    wa = std::sin((1 - t) * angle) / std::sin(angle);
    wb = std::sin(t * angle) / std::sin(angle);
  }
  for (std::size_t i = 0; i < 4; ++i) {
    a[i] = a[i] * wa + b[i] * wb;
  }
  return unit(a);
}

Vec3 lerp(const Vec3 a, const Vec3 b, const float t) {
  return a + (b - a) * t;
}
} // namespace

float SourcePmtTrack::period() const {
  float frames = 0;
  for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
    frames += keys[i].duration;
  }
  return frames;
}

void SourcePmtCursor::reset(const SourcePmtTrack &track, float frame, const bool loop) {
  key = 0;
  elapsed = 0;
  finished = false;
  if (track.keys.size() < 2) {
    return;
  }
  frame = std::max(frame, 0.0F);
  const float period = track.period();
  if (loop && period > 0) {
    frame = std::fmod(frame, period);
  }
  while (key + 2 < track.keys.size() && track.keys[key].duration > 0 &&
         frame >= track.keys[key].duration) {
    frame -= track.keys[key++].duration;
  }
  elapsed = std::min(frame, track.keys[key].duration);
}

void SourcePmtCursor::advance(const SourcePmtTrack &track, const float frames, const bool loop) {
  if (frames <= 0 || track.keys.size() < 2) {
    return;
  }
  const float previous = elapsed;
  elapsed += frames;
  while (track.keys[key].duration > 0 && elapsed >= track.keys[key].duration) {
    const float duration = track.keys[key].duration;
    if (key + 2 < track.keys.size()) {
      ++key;
    } else if (loop) {
      key = 0;
    } else {
      // 0x19f6f8 / 0x19fac8 restore the elapsed value saved before this
      // update, including when it already crossed earlier segments.
      elapsed = previous;
      finished = true;
      return;
    }
    elapsed -= duration;
  }
}

Mat4 SourcePmtCursor::sample(const SourcePmtTrack &track) const {
  Mat4 result = track.bind;
  if (track.keys.empty()) {
    return result;
  }
  const auto &a = track.keys[std::min(key, track.keys.size() - 1)];
  const auto &b = track.keys[std::min(key + 1, track.keys.size() - 1)];
  const float t = a.duration > 0 && track.keys.size() > 1 ? elapsed / a.duration : 0;
  if (a.rotation) {
    const auto q = interpolate(*a.rotation, b.rotation.value_or(*a.rotation), t);
    result = rotation(q[0], q[1], q[2], q[3]);
    if (!a.scale) {
      Vec3 bind_scale;
      const Vec3 rows[] = {{track.bind.at(0, 0), track.bind.at(0, 1), track.bind.at(0, 2)},
                           {track.bind.at(1, 0), track.bind.at(1, 1), track.bind.at(1, 2)},
                           {track.bind.at(2, 0), track.bind.at(2, 1), track.bind.at(2, 2)}};
      bind_scale = {length(rows[0]), length(rows[1]), length(rows[2])};
      if (dot(rows[0], cross(rows[1], rows[2])) < 0) {
        bind_scale.x = -bind_scale.x;
      }
      result = scaling(bind_scale) * result;
    }
  }
  if (a.scale) {
    if (!a.rotation) {
      for (int row = 0; row < 3; ++row) {
        const float size = length({result.at(row, 0), result.at(row, 1), result.at(row, 2)});
        if (size > 0) {
          for (int column = 0; column < 3; ++column) {
            result.at(row, column) /= size;
          }
        }
      }
    }
    result = scaling(lerp(*a.scale, b.scale.value_or(*a.scale), t)) * result;
  }
  const Vec3 bind{track.bind.at(3, 0), track.bind.at(3, 1), track.bind.at(3, 2)};
  const Vec3 position = a.position ? lerp(*a.position, b.position.value_or(*a.position), t) : bind;
  result.at(3, 0) = position.x;
  result.at(3, 1) = position.y;
  result.at(3, 2) = position.z;
  return result;
}

SourcePmtTrack parse_source_pmt(const json::JsonValue &value) {
  SourcePmtTrack track;
  if (const auto *bind = value.find("bind")) {
    track.bind.m = vector<16>(*bind);
  }
  const auto *keys = value.find("keys");
  if (!keys || !keys->is_array()) {
    throw std::runtime_error("Source PMT track has no keys array");
  }
  for (const auto &item : keys->array()) {
    SourcePmtKey key;
    key.duration = static_cast<float>(item.number_or("duration", 0));
    if (!std::isfinite(key.duration) || key.duration < 0) {
      throw std::runtime_error("Invalid source PMT duration");
    }
    if (const auto *q = item.find("rotation")) {
      key.rotation = unit(vector<4>(*q));
    }
    const auto read = [&](const char *name) -> std::optional<Vec3> {
      if (const auto *v = item.find(name)) {
        const auto xyz = vector<3>(*v);
        return Vec3{xyz[0], xyz[1], xyz[2]};
      }
      return {};
    };
    key.position = read("position");
    key.scale = read("scale");
    track.keys.push_back(key);
  }
  return track;
}

Mat4 sample_source_pmt(const SourcePmtTrack &track, const float frame) {
  SourcePmtCursor cursor;
  cursor.reset(track, frame, false);
  return cursor.sample(track);
}
} // namespace phantomcrash
