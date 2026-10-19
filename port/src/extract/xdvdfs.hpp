// Read-only reader for the Xbox XDVDFS game filesystem: trimmed .xiso images
// and full Redump-style images whose game partition starts at a standard XGD
// offset.
#pragma once

#include "binary.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace phantomcrash::extract {

struct DiscEntry {
  std::string path; // "/Media/x.bin", Latin-1 names as UTF-8
  std::uint32_t sector{};
  std::uint32_t size{};
  std::uint8_t attributes{};

  [[nodiscard]] bool is_directory() const noexcept {
    return (attributes & 0x10) != 0;
  }
};

class XdvdfsImage {
public:
  explicit XdvdfsImage(const std::filesystem::path &path);

  // Every file and directory, sorted by lower-cased path.
  [[nodiscard]] std::vector<DiscEntry> entries();
  [[nodiscard]] std::vector<DiscEntry> files();
  [[nodiscard]] Bytes read(const DiscEntry &entry);
  // Streams one file in chunks without holding it in memory.
  void copy_to(const DiscEntry &entry, const std::function<void(ByteView)> &sink);

  [[nodiscard]] std::uint64_t partition_offset() const noexcept {
    return partition_offset_;
  }

private:
  [[nodiscard]] Bytes read_extent(std::uint64_t sector, std::uint64_t size);
  void directory(const std::string &prefix, std::uint32_t sector, std::uint32_t size,
                 std::vector<DiscEntry> &found);

  std::filesystem::path path_;
  std::ifstream file_;
  std::uint64_t size_{};
  std::uint64_t partition_offset_{};
  std::uint32_t root_sector_{};
  std::uint32_t root_size_{};
};

} // namespace phantomcrash::extract
