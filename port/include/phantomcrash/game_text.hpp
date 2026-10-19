// The game's own text, read from the player's executable rather than shipped
// with the port. A data file's string may contain references such as
// `{xbe:0x19b848}`: each stands for the text at that address in default.xbe
// (Shift-JIS, NFKC, trimmed). phantomcrash_extract reads every referenced
// string when it converts the disc and writes them to tables/text.json, and
// DataRoots::document puts them back in place as it loads a document.
#pragma once

#include "phantomcrash/json.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace phantomcrash {

// The table's path among the converted assets (a mod may replace it).
inline constexpr std::string_view game_text_table = "tables/text.json";

// The executable addresses `text` references, in order of appearance.
[[nodiscard]] std::vector<std::uint32_t> game_text_references(std::string_view text);

// A reference as data files write it: {xbe:0x19b848}.
[[nodiscard]] std::string game_text_reference(std::uint32_t address);

// The folders whose data files the conversion reads references from: the
// shipped data and the mods overlaying it.
[[nodiscard]] std::vector<std::filesystem::path>
game_text_sources(const std::filesystem::path &root);

class GameText final {
public:
  GameText() = default;
  // tables/text.json: {"strings": {"0x19b848": "...", ...}}.
  [[nodiscard]] static GameText parse(const json::JsonValue &table);

  [[nodiscard]] const std::string *find(std::uint32_t address) const;
  // `text` with each reference replaced by its string. Throws naming the
  // first reference the table lacks.
  [[nodiscard]] std::string resolve(std::string_view text) const;
  // Every string value under `value` resolved in place (object keys stay).
  void resolve(json::JsonValue &value) const;

private:
  std::unordered_map<std::uint32_t, std::string> strings_;
};

// Whether any string value under `value` holds a reference.
[[nodiscard]] bool has_game_text_references(const json::JsonValue &value);

} // namespace phantomcrash
