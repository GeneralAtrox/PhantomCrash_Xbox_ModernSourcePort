#include "audio.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace phantomcrash::extract {
namespace {

constexpr std::array<int, 89> steps{
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
constexpr std::array<int, 8> index_adjust{-1, -1, -1, -1, 2, 4, 6, 8};
constexpr std::size_t adpcm_block = 36;
constexpr int adpcm_frames = 64;

bool tag_is(ByteView data, std::size_t at, std::string_view tag) {
  return at + tag.size() <= data.size() &&
         std::memcmp(data.data() + at, tag.data(), tag.size()) == 0;
}

void decode_nibble(const unsigned code, int &predictor, int &index) {
  const int step = steps[static_cast<std::size_t>(index)];
  int difference = step >> 3;
  if (code & 4) {
    difference += step;
  }
  if (code & 2) {
    difference += step >> 1;
  }
  if (code & 1) {
    difference += step >> 2;
  }
  predictor += (code & 8) ? -difference : difference;
  predictor = std::clamp(predictor, -32768, 32767);
  index = std::clamp(index + index_adjust[code & 7], 0, 88);
}

} // namespace

Wave parse_wav(ByteView data) {
  if (!tag_is(data, 0, "RIFF") || !tag_is(data, 8, "WAVE")) {
    throw FormatError("Not a RIFF WAVE file");
  }
  const std::size_t end = std::min<std::size_t>(data.size(), 8 + std::size_t{u32(data, 4)});
  bool have_format = false;
  Wave wave;
  for (std::size_t offset = 12; offset + 8 <= end;) {
    const std::size_t size = u32(data, offset + 4);
    const std::size_t body = offset + 8;
    const std::size_t body_end = std::min(data.size(), body + size);
    if (tag_is(data, offset, "fmt ")) {
      wave.format_tag = u16(data, body);
      wave.channels = u16(data, body + 2);
      wave.sample_rate = u32(data, body + 4);
      wave.block_align = u16(data, body + 12);
      wave.bits = u16(data, body + 14);
      have_format = true;
    } else if (tag_is(data, offset, "data")) {
      if (!have_format) {
        throw FormatError("data chunk before fmt chunk");
      }
      wave.data = data.subspan(body, body_end - std::min(body, body_end));
      return wave;
    }
    offset += 8 + size + (size & 1);
  }
  throw FormatError("No data chunk");
}

Bytes decode_xbox_adpcm(ByteView data, const int channels) {
  const auto count = static_cast<std::size_t>(channels);
  const std::size_t block_size = adpcm_block * count;
  Bytes out;
  if (count == 0 || data.size() < block_size) {
    return out;
  }
  out.reserve(data.size() / block_size * adpcm_frames * count * 2);
  std::vector<int> predictors(count), indices(count);
  std::vector<std::array<std::int16_t, adpcm_frames>> samples(count);
  for (std::size_t block = 0; block + block_size <= data.size(); block += block_size) {
    for (std::size_t channel = 0; channel < count; ++channel) {
      predictors[channel] = i16(data, block + channel * 4);
      indices[channel] = std::min<int>(u8(data, block + channel * 4 + 2), 88);
      samples[channel][0] = static_cast<std::int16_t>(predictors[channel]);
    }
    std::size_t position = block + count * 4;
    for (std::size_t group = 0; group < 8; ++group) {
      for (std::size_t channel = 0; channel < count; ++channel) {
        const std::uint32_t word = u32(data, position);
        position += 4;
        for (std::size_t nibble = 0; nibble < 8; ++nibble) {
          decode_nibble((word >> (nibble * 4)) & 0xF, predictors[channel], indices[channel]);
          // The 65th decoded sample is padding the Xbox never plays.
          const std::size_t frame = 1 + group * 8 + nibble;
          if (frame < adpcm_frames) {
            samples[channel][frame] = static_cast<std::int16_t>(predictors[channel]);
          }
        }
      }
    }
    for (std::size_t frame = 0; frame < adpcm_frames; ++frame) {
      for (std::size_t channel = 0; channel < count; ++channel) {
        const auto value = static_cast<std::uint16_t>(samples[channel][frame]);
        out.push_back(static_cast<std::uint8_t>(value & 0xFF));
        out.push_back(static_cast<std::uint8_t>(value >> 8));
      }
    }
  }
  return out;
}

Bytes to_pcm16(const Wave &wave) {
  if (wave.format_tag == wave_xbox_adpcm) {
    return decode_xbox_adpcm(wave.data, wave.channels);
  }
  if (wave.format_tag == wave_pcm && wave.bits == 16) {
    return Bytes(wave.data.begin(), wave.data.end());
  }
  throw FormatError("Unsupported WAV format tag " + std::to_string(wave.format_tag) + "/" +
                    std::to_string(wave.bits) + " bits");
}

Bytes encode_pcm16_wav(ByteView pcm, const int channels, const std::uint32_t sample_rate) {
  const auto align = static_cast<std::uint16_t>(channels * 2);
  const auto size = static_cast<std::uint32_t>(pcm.size());
  Bytes out;
  out.reserve(44 + pcm.size());
  const auto tag = [&](std::string_view text) { out.insert(out.end(), text.begin(), text.end()); };
  tag("RIFF");
  put_u32(out, 36 + size);
  tag("WAVE");
  tag("fmt ");
  put_u32(out, 16);
  put_u16(out, wave_pcm);
  put_u16(out, static_cast<std::uint16_t>(channels));
  put_u32(out, sample_rate);
  put_u32(out, sample_rate * align);
  put_u16(out, align);
  put_u16(out, 16);
  tag("data");
  put_u32(out, size);
  out.insert(out.end(), pcm.begin(), pcm.end());
  return out;
}

bool is_sound_bank(ByteView data) {
  return tag_is(data, 0, bank_magic);
}

std::vector<BankEntry> parse_bank(ByteView data) {
  if (!is_sound_bank(data)) {
    throw FormatError("Not an SE BIN effect bank");
  }
  const std::uint32_t count = u32(data, 8);
  if (12 + std::uint64_t{count} * 16 > data.size()) {
    throw FormatError("Directory of " + std::to_string(count) + " entries lies outside the file");
  }
  std::vector<BankEntry> entries;
  for (std::uint32_t index = 0; index < count; ++index) {
    const std::size_t record = 12 + std::size_t{index} * 16;
    BankEntry entry;
    entry.index = static_cast<int>(index);
    entry.flags = u32(data, record);
    entry.slot = u32(data, record + 4);
    entry.parameters = u32(data, record + 8);
    entry.offset = u32(data, record + 12);
    if (std::uint64_t{entry.offset} + 12 > data.size()) {
      throw FormatError("Entry " + std::to_string(index) + " lies outside the file");
    }
    const std::size_t riff_end =
        std::min<std::size_t>(data.size(), entry.offset + 8 + std::size_t{u32(data, entry.offset + 4)});
    entry.wave = parse_wav(data.subspan(entry.offset, riff_end - entry.offset));
    entries.push_back(entry);
  }
  return entries;
}

void write_pcm16_wav(const std::filesystem::path &path, const Wave &wave) {
  write_file(path, encode_pcm16_wav(to_pcm16(wave), wave.channels, wave.sample_rate));
}

} // namespace phantomcrash::extract
