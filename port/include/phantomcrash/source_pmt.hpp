#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"
#include <array>
#include <optional>
#include <vector>

namespace phantomcrash {
struct SourcePmtKey {
  float duration{};
  std::optional<std::array<float, 4>> rotation; // native row-vector XYZW
  std::optional<Vec3> position;
  std::optional<Vec3> scale;
};

struct SourcePmtTrack {
  Mat4 bind;
  std::vector<SourcePmtKey> keys;
  [[nodiscard]] float period() const;
};

// Primary and secondary controls have separate key/elapsed pairs. The last
// key is an interpolation endpoint; its duration is not traversed.
struct SourcePmtCursor {
  std::size_t key{};
  float elapsed{};
  bool finished{};
  void reset(const SourcePmtTrack &track, float frame, bool loop);
  void advance(const SourcePmtTrack &track, float frames, bool loop);
  [[nodiscard]] Mat4 sample(const SourcePmtTrack &track) const;
};

[[nodiscard]] SourcePmtTrack parse_source_pmt(const json::JsonValue &value);
[[nodiscard]] Mat4 sample_source_pmt(const SourcePmtTrack &track, float frame);
} // namespace phantomcrash
