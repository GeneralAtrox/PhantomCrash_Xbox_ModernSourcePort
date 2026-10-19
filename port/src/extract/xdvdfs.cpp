#include "xdvdfs.hpp"

#include "text.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <string_view>
#include <tuple>

namespace phantomcrash::extract {
namespace {

constexpr std::uint64_t sector_size = 2048;
constexpr std::uint64_t volume_sector = 32;
constexpr std::string_view magic = "MICROSOFT*XBOX*MEDIA";
// xiso, XGD1, XGD2 and XGD3 game-partition offsets.
constexpr std::array<std::uint64_t, 4> partition_offsets{0x0, 0x18300000, 0xFD90000, 0x2080000};
constexpr std::size_t entry_header = 14;
constexpr std::uint16_t empty_node = 0xFFFF;
constexpr std::uint64_t copy_chunk = 8 * 1024 * 1024;

std::string lower(std::string text) {
  std::ranges::transform(text, text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

} // namespace

XdvdfsImage::XdvdfsImage(const std::filesystem::path &path)
    : path_(path), file_(path, std::ios::binary) {
  if (!file_) {
    throw std::runtime_error("cannot open disc image " + path.string());
  }
  size_ = std::filesystem::file_size(path);
  bool found = false;
  for (const std::uint64_t offset : partition_offsets) {
    const std::uint64_t start = offset + volume_sector * sector_size;
    if (start + magic.size() > size_) {
      continue;
    }
    file_.seekg(static_cast<std::streamoff>(start));
    std::string probe(magic.size(), '\0');
    file_.read(probe.data(), static_cast<std::streamsize>(probe.size()));
    if (file_ && probe == magic) {
      partition_offset_ = offset;
      found = true;
      break;
    }
    file_.clear();
  }
  if (!found) {
    throw FormatError("No XDVDFS volume descriptor found in " + path.string());
  }
  Bytes root(8);
  file_.seekg(static_cast<std::streamoff>(partition_offset_ + volume_sector * sector_size +
                                          magic.size()));
  file_.read(reinterpret_cast<char *>(root.data()), 8);
  root_sector_ = u32(root, 0);
  root_size_ = u32(root, 4);
}

Bytes XdvdfsImage::read_extent(const std::uint64_t sector, const std::uint64_t size) {
  const std::uint64_t start = partition_offset_ + sector * sector_size;
  if (start + size > size_) {
    throw FormatError("Extent sector=" + std::to_string(sector) + " size=" +
                      std::to_string(size) + " lies outside the image");
  }
  Bytes data(static_cast<std::size_t>(size));
  file_.seekg(static_cast<std::streamoff>(start));
  file_.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(size));
  if (!file_) {
    throw FormatError("Unexpected end of image");
  }
  return data;
}

void XdvdfsImage::directory(const std::string &prefix, const std::uint32_t sector,
                            const std::uint32_t size, std::vector<DiscEntry> &found) {
  const Bytes table = read_extent(sector, size);
  std::vector<std::uint32_t> pending{0};
  std::set<std::uint32_t> visited;
  while (!pending.empty()) {
    const std::uint32_t node = pending.back();
    pending.pop_back();
    const std::size_t offset = static_cast<std::size_t>(node) * 4;
    if (!visited.insert(node).second) {
      throw FormatError("Directory tree loop at sector " + std::to_string(sector));
    }
    if (offset + entry_header > table.size()) {
      throw FormatError("Directory node " + std::to_string(node) + " is truncated");
    }
    const std::uint16_t left = u16(table, offset);
    const std::uint16_t right = u16(table, offset + 2);
    if (left == empty_node) {
      continue; // empty directory or sector padding
    }
    const std::uint8_t name_length = u8(table, offset + 13);
    const std::size_t name_start = offset + entry_header;
    if (name_length == 0 || name_start + name_length > table.size()) {
      throw FormatError("Directory node " + std::to_string(node) + " has a bad name");
    }
    const std::string_view name(reinterpret_cast<const char *>(table.data()) + name_start,
                                name_length);
    found.push_back({prefix + "/" + decode_latin1(name), u32(table, offset + 4),
                     u32(table, offset + 8), u8(table, offset + 12)});
    for (const std::uint16_t child : {left, right}) {
      if (child != 0) {
        pending.push_back(child);
      }
    }
  }
}

std::vector<DiscEntry> XdvdfsImage::entries() {
  std::vector<DiscEntry> found;
  std::vector<std::tuple<std::string, std::uint32_t, std::uint32_t>> pending{
      {"", root_sector_, root_size_}};
  std::set<std::uint32_t> visited;
  while (!pending.empty()) {
    const auto [prefix, sector, size] = pending.back();
    pending.pop_back();
    if (!visited.insert(sector).second) {
      throw FormatError("Directory table at sector " + std::to_string(sector) +
                        " is referenced twice");
    }
    const std::size_t first = found.size();
    directory(prefix, sector, size, found);
    for (std::size_t i = first; i < found.size(); ++i) {
      if (found[i].is_directory() && found[i].size != 0) {
        pending.emplace_back(found[i].path, found[i].sector, found[i].size);
      }
    }
  }
  std::ranges::stable_sort(found, {}, [](const DiscEntry &e) { return lower(e.path); });
  return found;
}

std::vector<DiscEntry> XdvdfsImage::files() {
  std::vector<DiscEntry> out;
  for (auto &entry : entries()) {
    if (!entry.is_directory()) {
      out.push_back(std::move(entry));
    }
  }
  return out;
}

Bytes XdvdfsImage::read(const DiscEntry &entry) {
  return read_extent(entry.sector, entry.size);
}

void XdvdfsImage::copy_to(const DiscEntry &entry, const std::function<void(ByteView)> &sink) {
  const std::uint64_t start = partition_offset_ + entry.sector * sector_size;
  if (start + entry.size > size_) {
    throw FormatError(entry.path + " extends past the end of the image");
  }
  file_.seekg(static_cast<std::streamoff>(start));
  std::uint64_t remaining = entry.size;
  Bytes chunk;
  while (remaining > 0) {
    chunk.resize(static_cast<std::size_t>(std::min(remaining, copy_chunk)));
    file_.read(reinterpret_cast<char *>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
    if (!file_) {
      throw FormatError("Unexpected end of image while reading " + entry.path);
    }
    sink(chunk);
    remaining -= chunk.size();
  }
}

} // namespace phantomcrash::extract
