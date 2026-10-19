// Game audio formats (audio.py): RIFF WAV, Xbox ADPCM and the "SE BIN"
// effect banks.
//
// Xbox ADPCM (format tag 0x69) is IMA ADPCM with fixed 36-byte blocks per
// channel: a 4-byte header (int16 first sample, uint8 step index, reserved)
// and 32 bytes of 4-bit codes, low nibble first, interleaved per channel in
// 4-byte groups. Playback uses 64 samples per channel: the header sample and
// the first 63 decoded codes.
//
// An effect bank (Media\sound\*.bin) starts with "SE BIN  ", u32 count and
// count 16-byte records {u32 flags, u32 slot, u32 parameters, u32 offset};
// each offset points at a complete RIFF WAV. The flags' byte +2 is the
// number of reusable voices (0x0de8d0 cycles that many slots).
#pragma once

#include "binary.hpp"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace phantomcrash::extract {

inline constexpr int wave_xbox_adpcm = 0x69;
inline constexpr int wave_pcm = 0x01;
inline constexpr std::string_view bank_magic = "SE BIN  ";

struct Wave {
  int format_tag{};
  int channels{};
  std::uint32_t sample_rate{};
  int block_align{};
  int bits{};
  ByteView data;
};

struct BankEntry {
  int index{};
  std::uint32_t flags{}, slot{}, parameters{}, offset{};
  Wave wave;

  [[nodiscard]] int voices() const noexcept {
    return static_cast<int>((flags >> 16) & 0xFF);
  }
};

[[nodiscard]] Wave parse_wav(ByteView data);
// Interleaved signed 16-bit PCM.
[[nodiscard]] Bytes decode_xbox_adpcm(ByteView data, int channels);
[[nodiscard]] Bytes to_pcm16(const Wave &wave);
[[nodiscard]] Bytes encode_pcm16_wav(ByteView pcm, int channels, std::uint32_t sample_rate);
[[nodiscard]] bool is_sound_bank(ByteView data);
[[nodiscard]] std::vector<BankEntry> parse_bank(ByteView data);
void write_pcm16_wav(const std::filesystem::path &path, const Wave &wave);

} // namespace phantomcrash::extract
