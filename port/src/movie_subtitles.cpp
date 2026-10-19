// Subtitles over a movie (movie_subtitles.hpp).
#include "phantomcrash/movie_subtitles.hpp"

#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <stdexcept>

namespace phantomcrash {

MovieSubtitles parse_movie_subtitles(const json::JsonValue &value) {
  MovieSubtitles subtitles;
  subtitles.table = json::required(value, "table").string();
  if (const json::JsonValue *position = value.find("position")) {
    subtitles.position = json::numbers<2>(*position, "subtitles.position");
  }
  subtitles.colour = parse_colour(value.string_or("colour", "#f0f0f0"));
  subtitles.layer = static_cast<int>(value.number_or("layer", subtitles.layer));
  return subtitles;
}

void add_subtitle_lines(MovieSubtitles &subtitles, const json::JsonValue &table) {
  subtitles.frame_rate = table.number_or("frame_rate", subtitles.frame_rate);
  if (subtitles.frame_rate <= 0.0) {
    throw std::runtime_error("subtitles: frame_rate must be above 0");
  }
  for (const json::JsonValue &line : json::required(table, "subtitles").array()) {
    subtitles.lines.push_back({static_cast<float>(json::required(line, "start").number()),
                               static_cast<float>(json::required(line, "end").number()),
                               json::required(line, "text").string()});
  }
}

const Subtitle *subtitle_at(const MovieSubtitles &subtitles, const double seconds) {
  const auto frame = static_cast<float>(seconds * subtitles.frame_rate);
  for (const Subtitle &line : subtitles.lines) {
    if (line.start <= frame && frame < line.end) {
      return &line;
    }
  }
  return nullptr;
}

} // namespace phantomcrash
