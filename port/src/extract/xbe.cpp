#include "xbe.hpp"

#include "text.hpp"

#include <array>
#include <cstdio>
#include <ctime>
#include <set>
#include <utility>

namespace phantomcrash::extract {
namespace {

constexpr std::size_t section_header_size = 56;
constexpr std::size_t library_version_size = 16;
constexpr std::array<std::pair<const char *, std::uint32_t>, 2> entry_keys{
    {{"retail", 0xA8FC57AB}, {"debug", 0x94859D4B}}};
constexpr std::array<std::pair<const char *, std::uint32_t>, 2> thunk_keys{
    {{"retail", 0x5B6D40B6}, {"debug", 0xEFB1F152}}};

std::string hex(std::uint32_t value, bool padded) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), padded ? "0x%08x" : "0x%x", value);
  return buffer;
}

class HeaderReader {
public:
  HeaderReader(ByteView data, std::uint32_t base) : data_(data), base_(base) {}

  [[nodiscard]] std::uint32_t word(std::size_t offset) const {
    return extract::u32(data_, offset);
  }
  [[nodiscard]] std::size_t offset(std::uint32_t address, std::size_t size = 1) const {
    if (address < base_ || address - base_ + size > data_.size()) {
      throw FormatError("Address " + hex(address, true) + " lies outside the XBE headers");
    }
    return address - base_;
  }
  [[nodiscard]] std::string c_string(std::uint32_t address) const {
    return address == 0 ? "" : decode_latin1(extract::c_string(data_, offset(address)));
  }

private:
  ByteView data_;
  std::uint32_t base_;
};

std::string iso_utc(std::uint32_t seconds) {
  const std::time_t time = seconds;
  std::tm parts{};
  gmtime_s(&parts, &time);
  char buffer[40];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S+00:00", &parts);
  return buffer;
}

} // namespace

std::string Xbe::title_code() const {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "-%03u", title_id & 0xFFFF);
  std::string letters;
  letters += static_cast<char>((title_id >> 24) & 0xFF);
  letters += static_cast<char>((title_id >> 16) & 0xFF);
  return decode_latin1(letters) + buffer;
}

std::optional<std::uint16_t> Xbe::xdk_build() const {
  std::set<std::uint16_t> builds;
  for (const auto &library : libraries) {
    if (library.name == "XAPILIB") {
      builds.insert(library.build);
    }
  }
  return builds.size() == 1 ? std::optional(*builds.begin()) : std::nullopt;
}

Json Xbe::to_json() const {
  Json libs = array();
  for (const auto &l : libraries) {
    push(libs, object({{"name", text(l.name)},
                       {"major", integer(l.major)},
                       {"minor", integer(l.minor)},
                       {"build", integer(l.build)},
                       {"flags", integer(l.flags)}}));
  }
  Json secs = array();
  for (const auto &s : sections) {
    push(secs, object({{"name", text(s.name)},
                       {"flags", text(hex(s.flags, false))},
                       {"virtual_address", text(hex(s.virtual_address, false))},
                       {"virtual_size", text(hex(s.virtual_size, false))},
                       {"raw_address", text(hex(s.raw_address, false))},
                       {"raw_size", text(hex(s.raw_size, false))}}));
  }
  const auto build = xdk_build();
  return object({{"base_address", text(hex(base_address, true))},
                 {"image_size", integer(image_size)},
                 {"timestamp_utc", text(timestamp_utc)},
                 {"build_kind", text(build_kind)},
                 {"entry_point", text(hex(entry_point, true))},
                 {"kernel_thunk_address", text(hex(kernel_thunk_address, true))},
                 {"title_id", text(hex(title_id, true))},
                 {"title_name", text(title_name)},
                 {"region_flags", integer(region_flags)},
                 {"disk_number", integer(disk_number)},
                 {"version", integer(version)},
                 {"debug_path", text(debug_path)},
                 {"debug_filename", text(debug_filename)},
                 {"libraries", libs},
                 {"sections", secs},
                 {"title_code", text(title_code())},
                 {"xdk_build", build ? integer(*build) : null()}});
}

Xbe parse_xbe(ByteView data) {
  if (data.size() < 4 || std::string_view(reinterpret_cast<const char *>(data.data()), 4) != "XBEH") {
    throw FormatError("Missing XBEH magic");
  }
  Xbe out;
  out.base_address = u32(data, 0x104);
  const HeaderReader reader(data, out.base_address);
  out.image_size = reader.word(0x10C);
  const std::uint32_t encoded = reader.word(0x128);
  std::size_t kind = entry_keys.size();
  for (std::size_t k = 0; k < entry_keys.size(); ++k) {
    const std::uint32_t decoded = encoded ^ entry_keys[k].second;
    if (decoded >= out.base_address && decoded < out.base_address + out.image_size) {
      kind = k;
      out.entry_point = decoded;
      break;
    }
  }
  if (kind == entry_keys.size()) {
    throw FormatError("Encoded entry point matches neither retail nor debug keys");
  }
  out.build_kind = entry_keys[kind].first;
  out.kernel_thunk_address = reader.word(0x158) ^ thunk_keys[kind].second;

  const std::size_t certificate = reader.offset(reader.word(0x118), 0xB0);
  const std::string title = decode_utf16le(
      std::string_view(reinterpret_cast<const char *>(data.data()) + certificate + 0x0C, 0x50));
  out.title_name = title.substr(0, title.find('\0'));

  const std::uint32_t library_count = reader.word(0x160);
  const std::size_t library_table =
      reader.offset(reader.word(0x164), library_count * library_version_size);
  for (std::uint32_t i = 0; i < library_count; ++i) {
    const std::size_t at = library_table + i * library_version_size;
    std::string name(reinterpret_cast<const char *>(data.data()) + at, 8);
    name.erase(name.find_last_not_of('\0') + 1);
    out.libraries.push_back({decode_latin1(name), u16(data, at + 8), u16(data, at + 10),
                             u16(data, at + 12), u16(data, at + 14)});
  }

  const std::uint32_t count = reader.word(0x11C);
  const std::size_t table = reader.offset(reader.word(0x120), count * section_header_size);
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::size_t at = table + i * section_header_size;
    XbeSection section{reader.c_string(u32(data, at + 20)), u32(data, at), u32(data, at + 4),
                       u32(data, at + 8), u32(data, at + 12), u32(data, at + 16)};
    if (static_cast<std::uint64_t>(section.raw_address) + section.raw_size > data.size()) {
      throw FormatError("Section " + std::to_string(i) + " raw data lies outside the file");
    }
    out.sections.push_back(std::move(section));
  }
  out.timestamp_utc = iso_utc(reader.word(0x114));
  out.title_id = u32(data, certificate + 0x08);
  out.region_flags = u32(data, certificate + 0xA0);
  out.disk_number = u32(data, certificate + 0xA8);
  out.version = u32(data, certificate + 0xAC);
  out.debug_path = reader.c_string(reader.word(0x14C));
  out.debug_filename = reader.c_string(reader.word(0x150));
  return out;
}

XbeImage::XbeImage(ByteView data) : data_(data), sections_(parse_xbe(data).sections) {}

bool XbeImage::mapped(const std::uint32_t address, const std::size_t size) const {
  for (const auto &s : sections_) {
    if (s.virtual_address <= address &&
        static_cast<std::uint64_t>(address) + size <=
            static_cast<std::uint64_t>(s.virtual_address) + s.raw_size) {
      return true;
    }
  }
  return false;
}

std::size_t XbeImage::offset(const std::uint32_t address, const std::size_t size) const {
  for (const auto &s : sections_) {
    if (s.virtual_address <= address &&
        static_cast<std::uint64_t>(address) + size <=
            static_cast<std::uint64_t>(s.virtual_address) + s.raw_size) {
      return s.raw_address + (address - s.virtual_address);
    }
  }
  throw FormatError("Address " + hex(address, true) + " is not in the file image");
}

void XbeImage::require(const std::uint32_t address, const std::size_t size) const {
  if (!mapped(address, size)) {
    throw FormatError("Address " + hex(address, true) + " is not in the file image");
  }
}

std::uint32_t XbeImage::u32(const std::uint32_t address) const {
  return extract::u32(data_, offset(address, 4));
}
std::int32_t XbeImage::i32(const std::uint32_t address) const {
  return extract::i32(data_, offset(address, 4));
}
std::uint16_t XbeImage::u16(const std::uint32_t address) const {
  return extract::u16(data_, offset(address, 2));
}
std::int16_t XbeImage::i16(const std::uint32_t address) const {
  return extract::i16(data_, offset(address, 2));
}
std::uint8_t XbeImage::u8(const std::uint32_t address) const {
  return extract::u8(data_, offset(address, 1));
}
std::int8_t XbeImage::i8(const std::uint32_t address) const {
  return extract::i8(data_, offset(address, 1));
}
double XbeImage::f32(const std::uint32_t address) const {
  return extract::f32(data_, offset(address, 4));
}

std::optional<std::string> XbeImage::string(const std::uint32_t address) const {
  if (!mapped(address, 1)) {
    return std::nullopt;
  }
  const std::string raw = extract::c_string(data_, offset(address, 1));
  if (raw.empty()) {
    return std::nullopt;
  }
  for (const char c : raw) {
    const auto b = static_cast<unsigned char>(c);
    if (b < 0x20 || b > 0x7E) {
      return std::nullopt;
    }
  }
  return raw;
}

std::string XbeImage::raw_string(const std::uint32_t address) const {
  return extract::c_string(data_, offset(address, 1));
}

std::vector<std::int32_t> XbeImage::ints(const std::uint32_t address,
                                         const std::size_t count) const {
  for (const auto &s : sections_) {
    const std::uint64_t end = static_cast<std::uint64_t>(address) + 4 * count;
    if (s.virtual_address <= address && end <= std::uint64_t{s.virtual_address} + s.virtual_size) {
      const std::uint32_t within = address - s.virtual_address;
      std::vector<std::int32_t> out(count, 0);
      for (std::size_t i = 0; i < count; ++i) {
        if (within + 4 * i + 4 <= s.raw_size) {
          out[i] = extract::i32(data_, s.raw_address + within + 4 * i);
        }
      }
      return out;
    }
  }
  throw FormatError("Address " + hex(address, true) + " is not in the executable's memory");
}

namespace {

template <typename T, typename Read>
std::vector<T> read_array(const XbeImage &image, const std::uint32_t address,
                          const std::size_t count, Read read) {
  const std::size_t start = image.offset(address, count * sizeof(T));
  std::vector<T> out;
  out.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    out.push_back(static_cast<T>(read(image.data(), start + i * sizeof(T))));
  }
  return out;
}

} // namespace

std::vector<std::int32_t> XbeImage::i32s(const std::uint32_t address, const std::size_t count) const {
  return read_array<std::int32_t>(*this, address, count, extract::i32);
}
std::vector<std::uint32_t> XbeImage::u32s(const std::uint32_t address, const std::size_t count) const {
  return read_array<std::uint32_t>(*this, address, count, extract::u32);
}
std::vector<std::int16_t> XbeImage::i16s(const std::uint32_t address, const std::size_t count) const {
  return read_array<std::int16_t>(*this, address, count, extract::i16);
}
std::vector<double> XbeImage::f32s(const std::uint32_t address, const std::size_t count) const {
  std::vector<double> out;
  const std::size_t start = offset(address, count * 4);
  for (std::size_t i = 0; i < count; ++i) {
    out.push_back(extract::f32(data_, start + i * 4));
  }
  return out;
}

const XbeSection &XbeImage::section(std::string_view name) const {
  for (const auto &s : sections_) {
    if (s.name == name) {
      return s;
    }
  }
  throw FormatError("No section " + std::string(name));
}

} // namespace phantomcrash::extract
