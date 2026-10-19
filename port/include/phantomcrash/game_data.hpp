// The game data a player installs from their own disc, under
// <root>/artifacts/europe: the disc's files (extracted once) and the assets
// converted from them (redone whenever a release changes the converter).
#pragma once

#include <filesystem>
#include <string>

namespace phantomcrash {

// The converted-asset layout this build reads, stored in the assets'
// manifest.json as "converter_version". Bump it whenever the converter's
// output changes: a new release then re-converts old assets from the disc
// files already extracted, without asking for the disc again.
// 1: the Python tools (0.2.0 and earlier) wrote no version.
// 2: the C++ extractor (phantomcrash_extract).
// 3: legs records carry stop_keep (0.5.7).
// 4: chatter.json carries the Area Rankers' defeat lines.
// 5: tables/text.json carries the game text the data files reference.
inline constexpr int converted_assets_version = 5;

struct GameDataPaths {
  std::filesystem::path europe;   // artifacts/europe
  std::filesystem::path files;    // the disc's files
  std::filesystem::path metadata; // disc.json, xbe.json
  std::filesystem::path assets;   // the converted assets
};

[[nodiscard]] GameDataPaths game_data_paths(const std::filesystem::path &root);

struct GameDataStatus {
  bool files{};         // every file metadata/disc.json lists is present at its size
  bool assets{};        // a complete conversion by this converter version
  int assets_version{}; // the assets' converter version; 0 when absent
  std::string problem;  // why the data is not usable, for messages; empty when it is
};

// Reads the manifests and checks file sizes; does not hash anything.
[[nodiscard]] GameDataStatus game_data_status(const std::filesystem::path &root);

} // namespace phantomcrash
