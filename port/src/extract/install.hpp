// Installs the game data under <root>/artifacts/europe, doing only what is
// out of date: the disc is extracted only when its files are missing or
// changed, and the assets are converted only when they are missing,
// unfinished or from another converter version. Work goes to a staging
// folder that replaces the old one when it is complete, so a failed or
// interrupted run leaves the previous data as it was. mods/ and the saves
// (in the user's app-data folder) are never touched.
#pragma once

#include "extract.hpp"

#include <filesystem>
#include <optional>
#include <stdexcept>

namespace phantomcrash::extract {

struct InstallOptions {
  std::optional<std::filesystem::path> disc; // needed only when the files must be extracted
  bool convert{};                            // convert even when the assets are current
  bool verify{};                             // hash every extracted file against disc.json
};

// Thrown when the disc's files must be extracted and no disc was given.
class DiscNeeded : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct InstallResult {
  bool extracted{}, converted{};
};

InstallResult install_game_data(const std::filesystem::path &root, const InstallOptions &options,
                                const Progress &progress);

// Hashes every file disc.json lists; the first mismatch, or "" when all match.
[[nodiscard]] std::string verify_extracted_files(const std::filesystem::path &root);

} // namespace phantomcrash::extract
