// Motion banks (binlink resources of type 4): the robots' animations
// (motion.py). Word 0 is the track count, then one file offset per motion;
// a motion is `tracks` 16-byte records {0, flags, u16 stream, float stream}.
// Flags 0x700 give a track a rotation, 0xe0 a translation and 0x3800 a
// scale, each three channels read from the track's two streams (0x0ae6c0).
#pragma once

#include "binary.hpp"
#include "py_json.hpp"

#include <cstdint>

namespace phantomcrash::extract {

inline constexpr std::uint32_t track_rotation = 0x700;
inline constexpr std::uint32_t track_translation = 0xE0;
inline constexpr std::uint32_t track_scale = 0x3800;

// One channel: a u16 header (count & 0x1fff, kind & 0xe000), the key
// frames, and the floats the kind needs (the interpolator at 0x0ab0ac).
// Advances both stream offsets.
[[nodiscard]] Json motion_channel(ByteView data, std::size_t &keys_at, std::size_t &values_at);

// {"track_count", "motions": [null | {"tracks", "frames"}]}.
[[nodiscard]] Json parse_motion_bank(ByteView data);

} // namespace phantomcrash::extract
