// Disc extraction and asset conversion: the C++ counterpart of the Python
// tools' `extract` and `convert` commands, writing the same files.
#pragma once

#include "py_json.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace phantomcrash::extract {

using Progress = std::function<void(const std::string &)>;

// Every file of the disc into `output`/files, with metadata/disc.json and
// metadata/xbe.json. Refuses a non-empty output directory. Returns
// {"disc": ..., "xbe": ...}.
Json extract_disc(const std::filesystem::path &disc, const std::filesystem::path &output,
                  const Progress &progress);

// Converts an extraction's `files` directory into `output` (textures, models,
// sounds, robots, stages and tables) and returns the manifest. With
// `replace`, an earlier conversion (one with a manifest.json) is deleted first.
// The game text the data files under `text_sources` reference is read from
// the executable into tables/text.json (game_text_sources).
Json convert_assets(const std::filesystem::path &files, const std::filesystem::path &output,
                    bool replace, const std::vector<std::filesystem::path> &text_sources,
                    const Progress &progress);

// Python's str(Path(text)) on Windows: backslashes, no repeated separators
// or "." parts, no trailing separator.
[[nodiscard]] std::string python_path(const std::filesystem::path &path);

} // namespace phantomcrash::extract
