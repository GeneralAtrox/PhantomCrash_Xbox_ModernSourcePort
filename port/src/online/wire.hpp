#pragma once

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace phantomcrash::online {

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

class WireWriter {
public:
  void u8(std::uint8_t value) {
    bytes_.push_back(value);
  }
  void u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value));
    u8(static_cast<std::uint8_t>(value >> 8));
  }
  void u32(std::uint32_t value) {
    u16(static_cast<std::uint16_t>(value));
    u16(static_cast<std::uint16_t>(value >> 16));
  }
  void u64(std::uint64_t value) {
    u32(static_cast<std::uint32_t>(value));
    u32(static_cast<std::uint32_t>(value >> 32));
  }
  void f32(float value) {
    if (!std::isfinite(value)) {
      throw std::runtime_error("Non-finite online input");
    }
    u32(std::bit_cast<std::uint32_t>(value));
  }
  void raw(std::span<const std::uint8_t> value) {
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }
  [[nodiscard]] std::vector<std::uint8_t> take() {
    return std::move(bytes_);
  }

private:
  std::vector<std::uint8_t> bytes_;
};

class WireReader {
public:
  explicit WireReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
  std::uint8_t u8() {
    if (position_ == bytes_.size()) {
      throw std::runtime_error("Truncated online record");
    }
    return bytes_[position_++];
  }
  std::uint16_t u16() {
    const auto low = u8();
    return static_cast<std::uint16_t>(low | (static_cast<unsigned>(u8()) << 8));
  }
  std::uint32_t u32() {
    const auto low = u16();
    return low | (static_cast<std::uint32_t>(u16()) << 16);
  }
  std::uint64_t u64() {
    const auto low = u32();
    return low | (static_cast<std::uint64_t>(u32()) << 32);
  }
  float f32() {
    const float value = std::bit_cast<float>(u32());
    if (!std::isfinite(value)) {
      throw std::runtime_error("Non-finite online input");
    }
    return value;
  }
  std::span<const std::uint8_t> raw(std::size_t count) {
    if (count > bytes_.size() - position_) {
      throw std::runtime_error("Truncated online record");
    }
    const auto out = bytes_.subspan(position_, count);
    position_ += count;
    return out;
  }
  void finish() const {
    if (position_ != bytes_.size()) {
      throw std::runtime_error("Trailing online record data");
    }
  }

private:
  std::span<const std::uint8_t> bytes_;
  std::size_t position_{};
};

} // namespace phantomcrash::online
