#pragma once

// Subtitles over a movie: the ending movie's (scene 11, 0x01c4fa), from the
// converter's tables/ending_subtitles.json and the style in the movie's
// script (data/story/ending.json).

#include "phantomcrash/json.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace phantomcrash {

struct Subtitle {
  float start{}; // frames into the movie it shows from ...
  float end{};   // ... and up to, not including
  std::string text;
};

struct MovieSubtitles {
  std::string table;                             // the converted table, under the assets
  double frame_rate{60.0};                       // the table's frames a second
  std::array<float, 2> position{320.0F, 416.0F}; // the line's centre and top
  std::uint32_t colour{0xf0f0f0};
  int layer{};
  std::vector<Subtitle> lines;
};

// The style from a movie script's "subtitles" member; the lines come from
// its table (add_subtitle_lines). Throws std::runtime_error on bad values.
[[nodiscard]] MovieSubtitles parse_movie_subtitles(const json::JsonValue &value);
void add_subtitle_lines(MovieSubtitles &subtitles, const json::JsonValue &table);

// The line showing `seconds` into the movie, or null.
[[nodiscard]] const Subtitle *subtitle_at(const MovieSubtitles &subtitles, double seconds);

} // namespace phantomcrash
