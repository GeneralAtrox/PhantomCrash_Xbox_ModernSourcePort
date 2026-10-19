#include "binary.hpp"

#include <algorithm>
#include <fstream>

namespace phantomcrash::extract {

std::string c_string(ByteView data, std::size_t offset) {
  if (offset > data.size()) {
    throw FormatError("string at " + std::to_string(offset) + " lies outside the data");
  }
  const auto start = data.begin() + static_cast<std::ptrdiff_t>(offset);
  const auto end = std::find(start, data.end(), std::uint8_t{0});
  return {start, end};
}

std::filesystem::path utf8_path(const std::string_view text) {
  return std::filesystem::path(
      std::u8string(reinterpret_cast<const char8_t *>(text.data()), text.size()));
}

Bytes read_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot open " + path.string());
  }
  in.seekg(0, std::ios::end);
  const auto size = static_cast<std::size_t>(in.tellg());
  in.seekg(0);
  Bytes data(size);
  in.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(size));
  if (!in) {
    throw std::runtime_error("cannot read " + path.string());
  }
  return data;
}

void write_file(const std::filesystem::path &path, ByteView data) {
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
  if (!out) {
    throw std::runtime_error("cannot write " + path.string());
  }
}

void write_text(const std::filesystem::path &path, const std::string &text) {
  // Text mode on Windows, as Python's Path.write_text writes: LF -> CR LF.
  std::string translated;
  translated.reserve(text.size() + text.size() / 16);
  for (const char c : text) {
    if (c == '\n') {
      translated += '\r';
    }
    translated += c;
  }
  write_file(path, {reinterpret_cast<const std::uint8_t *>(translated.data()), translated.size()});
}

void put_u16(Bytes &out, const std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value));
  out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void put_u32(Bytes &out, const std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

void put_f32(Bytes &out, const float value) {
  std::uint32_t bits{};
  std::memcpy(&bits, &value, sizeof(bits));
  put_u32(out, bits);
}

void put_u32_be(Bytes &out, const std::uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    out.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

} // namespace phantomcrash::extract
