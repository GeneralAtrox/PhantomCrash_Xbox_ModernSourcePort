// The Xbox executable (XBE) header, certificate and section table, and reads
// of the game's tables by virtual address.
#pragma once

#include "binary.hpp"
#include "py_json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash::extract {

struct XbeSection {
  std::string name;
  std::uint32_t flags{}, virtual_address{}, virtual_size{}, raw_address{}, raw_size{};
};

struct XbeLibrary {
  std::string name;
  std::uint16_t major{}, minor{}, build{}, flags{};
};

struct Xbe {
  std::uint32_t base_address{}, image_size{};
  std::string timestamp_utc, build_kind;
  std::uint32_t entry_point{}, kernel_thunk_address{}, title_id{};
  std::string title_name;
  std::uint32_t region_flags{}, disk_number{}, version{};
  std::string debug_path, debug_filename;
  std::vector<XbeLibrary> libraries;
  std::vector<XbeSection> sections;

  [[nodiscard]] std::string title_code() const;
  [[nodiscard]] std::optional<std::uint16_t> xdk_build() const;
  // xbe.Xbe.to_dict(): addresses as hex strings.
  [[nodiscard]] Json to_json() const;
};

[[nodiscard]] Xbe parse_xbe(ByteView data);

// game_tables._Image: the XBE's file image addressed by virtual address.
class XbeImage {
public:
  explicit XbeImage(ByteView data);

  [[nodiscard]] ByteView data() const noexcept {
    return data_;
  }
  // File offset of `size` bytes at `address`; throws when unmapped.
  [[nodiscard]] std::size_t offset(std::uint32_t address, std::size_t size = 4) const;
  [[nodiscard]] bool mapped(std::uint32_t address, std::size_t size = 1) const;
  // Throws unless `size` bytes at `address` are in the file image.
  void require(std::uint32_t address, std::size_t size) const;
  [[nodiscard]] std::uint32_t u32(std::uint32_t address) const;
  [[nodiscard]] std::int32_t i32(std::uint32_t address) const;
  [[nodiscard]] std::uint16_t u16(std::uint32_t address) const;
  [[nodiscard]] std::int16_t i16(std::uint32_t address) const;
  [[nodiscard]] std::uint8_t u8(std::uint32_t address) const;
  [[nodiscard]] std::int8_t i8(std::uint32_t address) const;
  [[nodiscard]] double f32(std::uint32_t address) const;
  // The printable ASCII C string at `address`; nullopt for null, unmapped or
  // binary data (game_tables._Image.string).
  [[nodiscard]] std::optional<std::string> string(std::uint32_t address) const;
  // The raw bytes up to the next zero (game_tables._Image.raw_string).
  [[nodiscard]] std::string raw_string(std::uint32_t address) const;
  // `count` ints at `address`; memory past a section's file data reads as
  // zeros (events._ints).
  [[nodiscard]] std::vector<std::int32_t> ints(std::uint32_t address, std::size_t count) const;
  // `count` values at `address`, all within the file image.
  [[nodiscard]] std::vector<std::int32_t> i32s(std::uint32_t address, std::size_t count) const;
  [[nodiscard]] std::vector<std::uint32_t> u32s(std::uint32_t address, std::size_t count) const;
  [[nodiscard]] std::vector<std::int16_t> i16s(std::uint32_t address, std::size_t count) const;
  [[nodiscard]] std::vector<double> f32s(std::uint32_t address, std::size_t count) const;
  [[nodiscard]] const XbeSection &section(std::string_view name) const;
  [[nodiscard]] const std::vector<XbeSection> &sections() const noexcept {
    return sections_;
  }

private:
  ByteView data_;
  std::vector<XbeSection> sections_;
};

} // namespace phantomcrash::extract
