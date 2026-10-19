// Data tables read from the European default.xbe (game_tables.py). The
// addresses belong to that exact build; load_game_tables refuses any other.
#pragma once

#include "py_json.hpp"
#include "xbe.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace phantomcrash::extract {

inline constexpr const char *europe_xbe_sha256 =
    "bdbe2c3eb2361395babfe6095d1a2da72f6581dccb34812b8d45e084c6c903c6";
inline constexpr std::uint32_t resource_model = 0;
inline constexpr std::uint32_t resource_texture_pack = 3;

// One loadable resource: a file on the disc or a slice of a binlink pack.
struct Resource {
  int id{};
  std::optional<std::string> path; // relative to Media
  std::uint32_t size{}, type{};
  std::optional<std::uint32_t> first_slot, slot_count; // texture packs
  std::optional<std::uint32_t> binlink_pack, binlink_offset;
  // Models: the texture pack their batches index, and the first model slot
  // their meshes fill and how many (the loader's record at 0x0af17d).
  std::optional<std::uint32_t> texture_resource, first_mesh_slot, mesh_count;

  [[nodiscard]] Json to_json() const;
};

struct Sprite {
  int id{};
  std::uint32_t slot{};
  double left{}, top{}, right{}, bottom{}, texture_width{}, texture_height{};

  [[nodiscard]] Json to_json() const;
};

struct MusicCredit {
  int track{};
  std::string title, artist;
  int jacket{-1}, jacket_small{-1}, artist_name{-1}, artist_link{-1};
  std::uint32_t artist_profile{}; // text script shared by an artist's tracks
};

struct GameTables {
  std::vector<std::string> music;
  std::vector<int> scene_music;
  std::vector<std::string> movies;
  std::vector<std::string> binlink_packs;
  std::vector<Resource> resources;
  std::vector<Sprite> sprites;
  std::vector<std::string> font_characters; // glyph N draws character N (UTF-8)
  // Each character's first glyph; text maps to glyphs this way.
  std::unordered_map<std::string, std::size_t> first_glyph;
  std::vector<std::pair<int, int>> font_metrics;
  std::vector<MusicCredit> music_credits;
  std::vector<int> volume_millibels;

  // The disc texture pack and texture index in it that fill `slot`.
  [[nodiscard]] std::optional<std::pair<const Resource *, std::uint32_t>>
  texture_for_slot(std::uint32_t slot) const;
};

[[nodiscard]] GameTables load_game_tables(ByteView xbe);

// The binlink texture packs that fill a slot some sprite draws from.
[[nodiscard]] std::vector<const Resource *> sprite_binlink_packs(const GameTables &tables);

// "d:\media\movie\x.sfd" -> "movie/x.sfd" (relative to Media).
[[nodiscard]] std::string media_relative(const std::string &path);

// "scroll/logo_tex.bin" -> "scroll/logo_tex".
[[nodiscard]] std::string pack_name(const std::string &relative);

} // namespace phantomcrash::extract
