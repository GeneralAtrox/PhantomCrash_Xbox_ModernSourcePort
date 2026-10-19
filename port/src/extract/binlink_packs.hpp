// Reads binlink resources (robot_convert._Packs): the resource table's pack
// and offset slice a pack file of Media/binlink, each read once. Safe to use
// from several threads.
#pragma once

#include "binary.hpp"
#include "game_tables.hpp"

#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace phantomcrash::extract {

class BinlinkPacks {
public:
  BinlinkPacks(std::filesystem::path media, const GameTables &tables);

  // The resource's bytes; nullopt when it is not in a pack or the pack file
  // is missing. Views stay valid while this object lives.
  [[nodiscard]] std::optional<ByteView> read(std::uint32_t resource_id);
  // The lower-case stem of the resource's pack file (b00_shinjuku).
  [[nodiscard]] std::optional<std::string> pack_stem(std::uint32_t resource_id) const;
  [[nodiscard]] std::optional<std::uint32_t> pack_of(std::uint32_t resource_id) const;
  [[nodiscard]] const Resource *resource(std::uint32_t resource_id) const;

private:
  std::filesystem::path media_;
  const GameTables &tables_;
  std::mutex mutex_;
  std::map<std::uint32_t, std::optional<Bytes>> files_;
};

} // namespace phantomcrash::extract
