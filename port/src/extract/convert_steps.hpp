// The steps of convert_assets (convert.py), shared by its source files.
#pragma once

#include "extract.hpp"
#include "game_tables.hpp"
#include "xbe.hpp"

#include <filesystem>
#include <string_view>
#include <vector>

namespace phantomcrash::extract {

// The files of `directory` whose names end in `suffix` (any case), sorted as
// Python sorts Windows paths (case-insensitively).
[[nodiscard]] std::vector<std::filesystem::path> sorted_files(const std::filesystem::path &directory,
                                                             std::string_view suffix);

// A path's file name as UTF-8.
[[nodiscard]] std::string utf8_name(const std::filesystem::path &path);

// tables/*.json from the executable; returns the counts for the manifest.
Json write_tables(const XbeImage &image, const GameTables &tables,
                  const std::filesystem::path &output);

// tables/text.json: the executable's strings that the data files under
// `sources` reference (phantomcrash/game_text.hpp); returns how many.
Json write_game_text(const XbeImage &image, const std::vector<std::filesystem::path> &sources,
                     const std::filesystem::path &output);

// Robots, stages, effects and the battle's tables (robot_convert.py).
Json convert_robots(const std::filesystem::path &media, ByteView executable,
                    const GameTables &tables, const std::filesystem::path &output,
                    const Progress &report);

} // namespace phantomcrash::extract
