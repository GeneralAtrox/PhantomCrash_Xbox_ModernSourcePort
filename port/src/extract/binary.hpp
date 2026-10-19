// Little-endian reads from byte buffers, and whole-file reads and writes.
#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace phantomcrash::extract {

using Bytes = std::vector<std::uint8_t>;
using ByteView = std::span<const std::uint8_t>;

class FormatError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

template <typename T> [[nodiscard]] T read_le(ByteView data, std::size_t offset) {
  if (offset > data.size() || data.size() - offset < sizeof(T)) {
    throw FormatError("read of " + std::to_string(sizeof(T)) + " bytes at " +
                      std::to_string(offset) + " lies outside " + std::to_string(data.size()));
  }
  T value{};
  std::memcpy(&value, data.data() + offset, sizeof(T));
  return value;
}

[[nodiscard]] inline std::uint8_t u8(ByteView data, std::size_t offset) {
  return read_le<std::uint8_t>(data, offset);
}
[[nodiscard]] inline std::int8_t i8(ByteView data, std::size_t offset) {
  return read_le<std::int8_t>(data, offset);
}
[[nodiscard]] inline std::uint16_t u16(ByteView data, std::size_t offset) {
  return read_le<std::uint16_t>(data, offset);
}
[[nodiscard]] inline std::int16_t i16(ByteView data, std::size_t offset) {
  return read_le<std::int16_t>(data, offset);
}
[[nodiscard]] inline std::uint32_t u32(ByteView data, std::size_t offset) {
  return read_le<std::uint32_t>(data, offset);
}
[[nodiscard]] inline std::int32_t i32(ByteView data, std::size_t offset) {
  return read_le<std::int32_t>(data, offset);
}
// A float32 widened to double, as Python's struct "<f" gives it.
[[nodiscard]] inline double f32(ByteView data, std::size_t offset) {
  return static_cast<double>(read_le<float>(data, offset));
}

// The bytes from `offset` up to the next zero (or the end).
[[nodiscard]] std::string c_string(ByteView data, std::size_t offset);

[[nodiscard]] Bytes read_file(const std::filesystem::path &path);
// A UTF-8 relative or absolute path ("/" or "\\" separated) as a path.
[[nodiscard]] std::filesystem::path utf8_path(std::string_view text);
// Creates the parent directories first.
void write_file(const std::filesystem::path &path, ByteView data);
// Text with each newline written as CR LF, as Python writes text files on
// Windows.
void write_text(const std::filesystem::path &path, const std::string &text);

void put_u16(Bytes &out, std::uint16_t value);
void put_u32(Bytes &out, std::uint32_t value);
void put_f32(Bytes &out, float value);
void put_u32_be(Bytes &out, std::uint32_t value);

} // namespace phantomcrash::extract
