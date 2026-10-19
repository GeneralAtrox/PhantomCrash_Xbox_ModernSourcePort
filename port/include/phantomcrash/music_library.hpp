#pragma once

#include "phantomcrash/json.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace phantomcrash {

// The battle programs BATTLE MUSIC edits (the original's words at + 0xaa8,
// 8 x 12) and their names (+ 0xd78, 9 glyphs each).
inline constexpr int music_program_count = 8;
inline constexpr int music_program_length = 12;
inline constexpr int music_program_name_length = 9;

// Sonic Amp's chart, the day's best selection (+ 0xc28): ten tracks drawn
// from the first 77 credits, at least one of the first five among them
// (0x2a540); a new game and every night draw it again.
inline constexpr int music_chart_size = 10;
inline constexpr int music_chart_pool = 77;
inline constexpr int music_chart_hits = 5;

// How far the player has got with a track (the original's byte per credit
// at + 0x9d4): bought and not yet seen in BATTLE MUSIC, or seen there.
enum class TrackState { fresh = 1, seen = 2 };

// The player's music: the tracks owned, by index in the converted
// tables/music.json credits, and the eight programs a battle plays from.
struct MusicLibrary {
  std::map<int, TrackState> owned;
  std::array<std::vector<int>, music_program_count> programs;
  // Empty names show as PROGRAM 1 to PROGRAM 8.
  std::array<std::string, music_program_count> names;
  int program{};          // the one battles play (+ 0xd74)
  bool random{};          // RANDOM rather than SEQUENTIAL (+ 0xd70)
  std::vector<int> chart; // Sonic Amp's, No. 1 first

  [[nodiscard]] bool owns(const int track) const {
    return owned.contains(track);
  }
  // The owned tracks in credit order.
  [[nodiscard]] std::vector<int> owned_tracks() const;
  // Leaving BATTLE MUSIC marks every new track seen (0x369bd).
  void see_all();

  bool operator==(const MusicLibrary &) const = default;
};

// Reads a library, ignoring what does not fit: tracks outside
// [0, track_count), programs past the eighth and tracks past the twelfth.
[[nodiscard]] MusicLibrary music_from_json(const json::JsonValue &value, int track_count = 256);
[[nodiscard]] json::JsonValue music_to_json(const MusicLibrary &music);

// Draws the chart again from `seed`.
void roll_chart(MusicLibrary &music, std::uint32_t seed);

} // namespace phantomcrash
