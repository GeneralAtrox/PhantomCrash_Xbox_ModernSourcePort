// SHA-256 (FIPS 180-4), for the extraction's file hashes and the XBE check.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace phantomcrash::extract {

// Whether this CPU has the SHA extensions (sha256_x86.cpp).
[[nodiscard]] bool sha256_hardware_available();
// Hashes `count` 64-byte blocks into `state` with the SHA extensions.
void sha256_hardware_blocks(std::array<std::uint32_t, 8> &state, const std::uint8_t *data,
                            std::size_t count);

class Sha256 {
public:
  // `hardware` false keeps to the portable rounds (the tests compare both).
  explicit Sha256(bool hardware = true);
  void update(std::span<const std::uint8_t> data);
  // Lower-case hex of the digest; the object is spent afterwards.
  [[nodiscard]] std::string hex();

private:
  void blocks(const std::uint8_t *data, std::size_t count);
  void block(const std::uint8_t *data);

  std::array<std::uint32_t, 8> state_{};
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffered_{};
  std::uint64_t length_{};
  bool hardware_{};
};

[[nodiscard]] std::string sha256_hex(std::span<const std::uint8_t> data);

} // namespace phantomcrash::extract
