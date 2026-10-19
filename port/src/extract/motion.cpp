#include "motion.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace phantomcrash::extract {
namespace {

// Researcher-assigned names of the interpolator's kinds.
const char *kind_name(const unsigned kind) {
  switch (kind) {
  case 0x2000:
    return "step";
  case 0x4000:
    return "linear";
  case 0x6000:
    return "smooth";
  case 0x8000:
    return "hermite_out";
  case 0xA000:
    return "hermite_in";
  case 0xC000:
    return "hermite_split";
  default:
    return "hermite";
  }
}

std::size_t float_count(const unsigned kind, const std::size_t count) {
  switch (kind) {
  case 0x2000:
  case 0x4000:
  case 0x6000:
    return count + 1;
  case 0x8000:
  case 0xA000:
    return 2 * count + 1;
  case 0xC000:
    return 3 * count + 1;
  default:
    return 2 * count + 2;
  }
}

void need(ByteView data, const std::size_t offset, const std::size_t size) {
  if (offset + size > data.size()) {
    throw FormatError("Motion data lies outside the bank");
  }
}

Json zero_channel() {
  return object({{"kind", text("constant")}, {"values", reals({0.0})}});
}

Json track_json(ByteView data, const std::size_t record) {
  const std::uint32_t flags = u32(data, record + 4);
  std::size_t keys_at = u32(data, record + 8);
  std::size_t values_at = u32(data, record + 12);
  const bool streams = keys_at != 0 && values_at != 0;
  Json track = object({{"flags", integer(flags)}});
  const std::array<std::pair<const char *, std::uint32_t>, 3> groups{
      {{"rotation", track_rotation}, {"translation", track_translation}, {"scale", track_scale}}};
  for (const auto &[name, mask] : groups) {
    if ((flags & mask) == 0) {
      continue;
    }
    Json channels = array();
    for (int axis = 0; axis < 3; ++axis) {
      // No streams: the loader leaves the pointers null and every channel
      // reads 0.0.
      push(channels, streams ? motion_channel(data, keys_at, values_at) : zero_channel());
    }
    put(track, name, std::move(channels));
  }
  return track;
}

std::int64_t last_frame(const Json &tracks) {
  std::int64_t last = 0;
  for (const Json &track : tracks.array()) {
    for (const char *name : {"rotation", "translation", "scale"}) {
      const Json *channels = track.find(name);
      if (channels == nullptr) {
        continue;
      }
      for (const Json &channel : channels->array()) {
        if (const Json *keys = channel.find("keys")) {
          for (const Json &key : keys->array()) {
            last = std::max(last, static_cast<std::int64_t>(key.number()));
          }
        }
      }
    }
  }
  return last;
}

} // namespace

Json motion_channel(ByteView data, std::size_t &keys_at, std::size_t &values_at) {
  need(data, keys_at, 2);
  const std::uint16_t header = u16(data, keys_at);
  const std::size_t count = header & 0x1FFF;
  const unsigned kind = header & 0xE000;
  if (kind == 0) {
    keys_at += 2;
    if (count == 0) {
      return zero_channel();
    }
    need(data, values_at, 4);
    const double value = f32(data, values_at);
    values_at += 4;
    return object({{"kind", text("constant")}, {"values", reals({value})}});
  }
  need(data, keys_at + 2, 2 * count);
  Json keys = array();
  for (std::size_t k = 0; k < count; ++k) {
    push(keys, integer(u16(data, keys_at + 2 + 2 * k)));
  }
  const std::size_t floats = float_count(kind, count);
  need(data, values_at, 4 * floats);
  std::vector<double> values;
  values.reserve(floats);
  for (std::size_t k = 0; k < floats; ++k) {
    values.push_back(f32(data, values_at + 4 * k));
  }
  keys_at += 2 * (count + 1);
  values_at += 4 * floats;
  return object({{"kind", text(kind_name(kind))}, {"keys", keys}, {"values", reals(values)}});
}

Json parse_motion_bank(ByteView data) {
  if (data.size() < 8) {
    throw FormatError("Motion bank is too short");
  }
  const std::uint32_t track_count = u32(data, 0);
  if (!(0 < track_count && track_count < 64)) {
    throw FormatError("Unlikely track count " + std::to_string(track_count));
  }
  std::vector<std::uint32_t> offsets;
  std::size_t first = data.size();
  for (std::size_t index = 1; 4 * index < first && 4 * index + 4 <= data.size(); ++index) {
    const std::uint32_t offset = u32(data, 4 * index);
    offsets.push_back(offset);
    if (offset != 0 && offset < first) {
      first = offset;
    }
  }
  Json motions = array();
  for (const std::uint32_t offset : offsets) {
    if (offset == 0) {
      push(motions, null());
      continue;
    }
    if (std::uint64_t{offset} + 16ull * track_count > data.size()) {
      throw FormatError("Motion at " + std::to_string(offset) + " lies outside the bank");
    }
    Json tracks = array();
    for (std::uint32_t t = 0; t < track_count; ++t) {
      push(tracks, track_json(data, offset + 16 * std::size_t{t}));
    }
    const std::int64_t frames = last_frame(tracks);
    push(motions, object({{"tracks", std::move(tracks)}, {"frames", integer(frames)}}));
  }
  return object({{"track_count", integer(track_count)}, {"motions", motions}});
}

} // namespace phantomcrash::extract
