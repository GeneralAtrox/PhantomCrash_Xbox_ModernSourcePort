#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"
#include "phantomcrash/source_pmt.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace phantomcrash {

// A robot motion bank as the converter writes it (motions/<resource>.json,
// see src/phantomcrash_research/motion.py). A skeleton node names the track
// that drives it; values are sampled at a time in frames.

enum class ChannelKind {
  constant,
  step,
  linear,
  smooth,
  hermite_out,
  hermite_in,
  hermite_split,
  hermite,
};

// One animated float: key frames and the values (and tangents) between them,
// interpolated like the original's reader at 0x0ab0ac.
struct MotionChannel {
  ChannelKind kind{ChannelKind::constant};
  std::vector<float> keys; // frame of value 1, 2, ...; value 0 is at frame 0
  std::vector<float> values;

  [[nodiscard]] float sample(float frame) const;
};

inline constexpr std::uint32_t track_rotation = 0x700;
inline constexpr std::uint32_t track_translation = 0xe0;
inline constexpr std::uint32_t track_scale = 0x3800;

struct MotionTrack {
  std::uint32_t flags{};
  std::optional<std::array<MotionChannel, 3>> rotation;
  std::optional<std::array<MotionChannel, 3>> translation;
  std::optional<std::array<MotionChannel, 3>> scale;
  std::optional<SourcePmtTrack> source_pmt;
};

// A track sampled at one frame, as the file stores it. The skeleton applies
// the game's axis conventions.
struct TrackSample {
  std::optional<Vec3> rotation;
  std::optional<Vec3> translation;
  std::optional<Vec3> scale;
};

struct Motion {
  int frames{};
  std::vector<MotionTrack> tracks;

  [[nodiscard]] TrackSample sample(int track, float frame) const;
};

struct MotionBank {
  int track_count{};
  std::vector<std::optional<Motion>> motions; // by motion id; empty ids are nullopt

  [[nodiscard]] const Motion *motion(int id) const;
};

// One channel as the converter writes it ({"kind", "keys", "values"}).
// Throws std::runtime_error when it has too few values.
[[nodiscard]] MotionChannel parse_motion_channel(const json::JsonValue &value);

// Throws std::runtime_error when the document is not a motion bank.
[[nodiscard]] MotionBank parse_motion_bank(const json::JsonValue &document);

// Blends two samples of the same track (0: a, 1: b). Missing groups take the
// other sample's value.
[[nodiscard]] TrackSample blend(const TrackSample &a, const TrackSample &b, float weight);

} // namespace phantomcrash
