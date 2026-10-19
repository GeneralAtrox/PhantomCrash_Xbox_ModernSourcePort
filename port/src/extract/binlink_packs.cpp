#include "binlink_packs.hpp"

#include <algorithm>
#include <cctype>

namespace phantomcrash::extract {

BinlinkPacks::BinlinkPacks(std::filesystem::path media, const GameTables &tables)
    : media_(std::move(media)), tables_(tables) {}

const Resource *BinlinkPacks::resource(const std::uint32_t resource_id) const {
  return resource_id < tables_.resources.size() ? &tables_.resources[resource_id] : nullptr;
}

std::optional<std::uint32_t> BinlinkPacks::pack_of(const std::uint32_t resource_id) const {
  const Resource *found = resource(resource_id);
  return found != nullptr ? found->binlink_pack : std::nullopt;
}

std::optional<std::string> BinlinkPacks::pack_stem(const std::uint32_t resource_id) const {
  const auto pack = pack_of(resource_id);
  if (!pack) {
    return std::nullopt;
  }
  std::string stem = utf8_path(tables_.binlink_packs.at(*pack)).stem().string();
  std::ranges::transform(stem, stem.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return stem;
}

std::optional<ByteView> BinlinkPacks::read(const std::uint32_t resource_id) {
  const Resource *found = resource(resource_id);
  if (found == nullptr || !found->binlink_pack || !found->binlink_offset) {
    return std::nullopt;
  }
  const std::uint32_t pack = *found->binlink_pack;
  const std::optional<Bytes> *file = nullptr;
  {
    const std::scoped_lock lock(mutex_);
    auto [entry, added] = files_.try_emplace(pack);
    if (added) {
      const auto path = media_ / utf8_path(tables_.binlink_packs.at(pack));
      if (std::filesystem::is_regular_file(path)) {
        entry->second = read_file(path);
      }
    }
    file = &entry->second;
  }
  if (!*file) {
    return std::nullopt;
  }
  // Python's slice: clipped at the end of the file.
  const ByteView data = **file;
  const std::size_t start = std::min<std::size_t>(*found->binlink_offset, data.size());
  return data.subspan(start, std::min<std::size_t>(found->size, data.size() - start));
}

} // namespace phantomcrash::extract
