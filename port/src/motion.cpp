#include "phantomcrash/motion.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

ChannelKind kind_named(const std::string &name) {
  static constexpr std::array<std::pair<const char *, ChannelKind>, 8> names{{
      {"constant", ChannelKind::constant},
      {"step", ChannelKind::step},
      {"linear", ChannelKind::linear},
      {"smooth", ChannelKind::smooth},
      {"hermite_out", ChannelKind::hermite_out},
      {"hermite_in", ChannelKind::hermite_in},
      {"hermite_split", ChannelKind::hermite_split},
      {"hermite", ChannelKind::hermite},
  }};
  for (const auto &[text, kind] : names) {
    if (name == text) {
      return kind;
    }
  }
  throw std::runtime_error("Unknown motion channel kind: " + name);
}

// Floats stored per key: the value, then its tangents.
std::size_t stride(const ChannelKind kind) {
  switch (kind) {
  case ChannelKind::hermite_out:
  case ChannelKind::hermite_in:
  case ChannelKind::hermite:
    return 2;
  case ChannelKind::hermite_split:
    return 3;
  default:
    return 1;
  }
}

std::vector<float> floats(const json::JsonValue *array) {
  std::vector<float> out;
  if (array != nullptr && array->is_array()) {
    out.reserve(array->array().size());
    for (const json::JsonValue &value : array->array()) {
      out.push_back(static_cast<float>(value.number()));
    }
  }
  return out;
}

MotionChannel parse_channel(const json::JsonValue &value) {
  MotionChannel channel;
  channel.kind = kind_named(value.string_or("kind", "constant"));
  channel.keys = floats(value.find("keys"));
  channel.values = floats(value.find("values"));
  const std::size_t needed =
      channel.kind == ChannelKind::constant ? 1 : channel.keys.size() * stride(channel.kind) + 1;
  if (channel.values.size() < needed) {
    throw std::runtime_error("Motion channel has too few values");
  }
  return channel;
}

std::optional<std::array<MotionChannel, 3>> parse_group(const json::JsonValue &track,
                                                        const char *name) {
  const json::JsonValue *group = track.find(name);
  if (group == nullptr || !group->is_array()) {
    return std::nullopt;
  }
  if (group->array().size() != 3) {
    throw std::runtime_error("A motion track group needs three channels");
  }
  std::array<MotionChannel, 3> channels;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    channels[axis] = parse_channel(group->array()[axis]);
  }
  return channels;
}

std::optional<Vec3> sample_group(const std::optional<std::array<MotionChannel, 3>> &group,
                                 const float frame) {
  if (!group) {
    return std::nullopt;
  }
  return Vec3{(*group)[0].sample(frame), (*group)[1].sample(frame), (*group)[2].sample(frame)};
}

std::optional<Vec3> mix(const std::optional<Vec3> &a, const std::optional<Vec3> &b,
                        const float weight) {
  if (!a) {
    return b;
  }
  if (!b) {
    return a;
  }
  return *a + (*b - *a) * weight;
}

} // namespace

float MotionChannel::sample(const float frame) const {
  if (kind == ChannelKind::constant || keys.empty()) {
    return values.empty() ? 0.0F : values.front();
  }
  // The key whose interval holds the frame; past the last key the motion
  // holds its final value (the original clamps the time to the motion end).
  const std::size_t count = keys.size();
  std::size_t k = 0;
  while (k < count && keys[k] < frame) {
    ++k;
  }
  const std::size_t s = stride(kind);
  if (k == count) {
    return values[count * s];
  }
  const float start = k == 0 ? 0.0F : keys[k - 1];
  // Value k sits at `start`; value k + 1 at keys[k].
  const float v0 = values[k * s];
  if (frame <= start || kind == ChannelKind::step) {
    return v0;
  }
  const float v1 = values[(k + 1) * s];
  const float elapsed = frame - start;
  const float f = elapsed / (keys[k] - start);
  const float g = f - 1.0F;
  if (kind == ChannelKind::linear) {
    return v0 + (v1 - v0) * f;
  }
  // Smoothstep from v0 to v1: v0 + (v1 - v0)(3f^2 - 2f^3).
  const float eased = (2.0F * g - 1.0F) * (v0 - v1) * f * f + v0;
  switch (kind) {
  case ChannelKind::hermite_out:
    return eased + g * elapsed * f * values[k * s + 1];
  case ChannelKind::hermite_in:
    return eased + g * elapsed * g * values[k * s + 1];
  case ChannelKind::hermite_split:
    return eased + g * elapsed * (g * values[k * s + 1] + f * values[k * s + 2]);
  case ChannelKind::hermite:
    return eased + g * elapsed * (g * values[k * s + 1] + f * values[(k + 1) * s + 1]);
  default:
    return eased;
  }
}

TrackSample Motion::sample(const int track, const float frame) const {
  TrackSample out;
  if (track < 0 || track >= static_cast<int>(tracks.size())) {
    return out;
  }
  const MotionTrack &entry = tracks[static_cast<std::size_t>(track)];
  const float clamped = std::clamp(frame, 0.0F, static_cast<float>(frames));
  out.rotation = sample_group(entry.rotation, clamped);
  out.translation = sample_group(entry.translation, clamped);
  out.scale = sample_group(entry.scale, clamped);
  return out;
}

const Motion *MotionBank::motion(const int id) const {
  if (id < 0 || id >= static_cast<int>(motions.size()) || !motions[static_cast<std::size_t>(id)]) {
    return nullptr;
  }
  return &*motions[static_cast<std::size_t>(id)];
}

MotionChannel parse_motion_channel(const json::JsonValue &value) {
  return parse_channel(value);
}

MotionBank parse_motion_bank(const json::JsonValue &document) {
  MotionBank bank;
  bank.track_count = static_cast<int>(document.number_or("track_count", 0));
  const json::JsonValue *motions = document.find("motions");
  if (bank.track_count <= 0 || motions == nullptr || !motions->is_array()) {
    throw std::runtime_error("Not a motion bank");
  }
  for (const json::JsonValue &value : motions->array()) {
    if (value.is_null()) {
      bank.motions.emplace_back();
      continue;
    }
    Motion motion;
    motion.frames = static_cast<int>(value.number_or("frames", 0));
    const json::JsonValue *tracks = value.find("tracks");
    if (tracks == nullptr || !tracks->is_array() ||
        static_cast<int>(tracks->array().size()) != bank.track_count) {
      throw std::runtime_error("A motion has the wrong number of tracks");
    }
    for (const json::JsonValue &track : tracks->array()) {
      MotionTrack entry;
      entry.flags = static_cast<std::uint32_t>(track.number_or("flags", 0));
      entry.rotation = parse_group(track, "rotation");
      entry.translation = parse_group(track, "translation");
      entry.scale = parse_group(track, "scale");
      if (const auto *pmt = track.find("source_pmt")) {
        entry.source_pmt = parse_source_pmt(*pmt);
      }
      motion.tracks.push_back(std::move(entry));
    }
    bank.motions.emplace_back(std::move(motion));
  }
  return bank;
}

TrackSample blend(const TrackSample &a, const TrackSample &b, const float weight) {
  TrackSample out;
  out.rotation = mix(a.rotation, b.rotation, weight);
  out.translation = mix(a.translation, b.translation, weight);
  out.scale = mix(a.scale, b.scale, weight);
  return out;
}

} // namespace phantomcrash
