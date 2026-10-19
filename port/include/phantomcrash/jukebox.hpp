#pragma once

#include "phantomcrash/json.hpp"

#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace phantomcrash {

// data/audio/radio.json.
struct JukeboxTuning {
  int playlist{16};
  double gap_seconds{5.0};
  float volume{1.0F};
  std::vector<std::string> shuffle_on; // screens that deal a new playlist as they open
  std::vector<std::string> plays_on;   // screens the radio plays on
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] JukeboxTuning parse_jukebox(const json::JsonValue &document);

// The town's radio (0x1ba45, 0x1bae7, 0x2a972). Opening Club Wired deals a
// playlist of distinct songs from the credits and plays one of them at
// random at once, played through once. On the screens the radio plays on,
// once no music has played for gap_seconds, it plays another at random. The
// original keeps "played" flags it never sets, so a song can come again.
class Jukebox final {
public:
  Jukebox(JukeboxTuning tuning, std::vector<std::string> songs, std::uint32_t seed);

  // As a screen opens: the song to start now, if any.
  [[nodiscard]] std::optional<std::string> open(const std::string &screen);
  // Each tick; `silent` when no music plays. The song to start, if any.
  [[nodiscard]] std::optional<std::string> update(const std::string &screen, double tick_seconds,
                                                  bool silent);

  [[nodiscard]] const std::vector<std::string> &playlist() const noexcept {
    return playlist_;
  }
  [[nodiscard]] float volume() const noexcept {
    return tuning_.volume;
  }

private:
  [[nodiscard]] std::optional<std::string> pick();

  JukeboxTuning tuning_;
  std::vector<std::string> songs_;
  std::vector<std::string> playlist_;
  std::minstd_rand random_;
  double silence_{};
};

} // namespace phantomcrash
