#include "phantomcrash/music_library.hpp"

#include <algorithm>
#include <charconv>
#include <numeric>
#include <random>

namespace phantomcrash {

std::vector<int> MusicLibrary::owned_tracks() const {
  std::vector<int> tracks;
  for (const auto &[track, state] : owned) {
    tracks.push_back(track);
  }
  return tracks;
}

void MusicLibrary::see_all() {
  for (auto &[track, state] : owned) {
    state = TrackState::seen;
  }
}

MusicLibrary music_from_json(const json::JsonValue &value, const int track_count) {
  MusicLibrary music;
  const auto valid = [track_count](const double track) {
    return track >= 0.0 && track < static_cast<double>(track_count);
  };
  if (const auto *owned = value.find("owned"); owned != nullptr && owned->is_object()) {
    for (const auto &[key, state] : owned->object()) {
      int track = -1;
      std::from_chars(key.data(), key.data() + key.size(), track);
      if (valid(track) && state.is_number()) {
        music.owned[track] = state.number() == 1.0 ? TrackState::fresh : TrackState::seen;
      }
    }
  }
  if (const auto *programs = value.find("programs"); programs != nullptr && programs->is_array()) {
    std::size_t index = 0;
    for (const auto &program : programs->array()) {
      if (index == music.programs.size()) {
        break;
      }
      auto &tracks = music.programs[index++];
      if (!program.is_array()) {
        continue;
      }
      for (const auto &track : program.array()) {
        if (track.is_number() && valid(track.number()) &&
            tracks.size() < static_cast<std::size_t>(music_program_length)) {
          tracks.push_back(static_cast<int>(track.number()));
        }
      }
    }
  }
  if (const auto *names = value.find("names"); names != nullptr && names->is_array()) {
    std::size_t index = 0;
    for (const auto &name : names->array()) {
      if (index == music.names.size()) {
        break;
      }
      music.names[index++] = name.is_string() ? name.string() : std::string{};
    }
  }
  const int program = static_cast<int>(value.number_or("program", 0));
  music.program = program >= 0 && program < music_program_count ? program : 0;
  music.random = value.boolean_or("random", false);
  if (const auto *chart = value.find("chart"); chart != nullptr && chart->is_array()) {
    for (const auto &track : chart->array()) {
      if (track.is_number() && valid(track.number()) &&
          music.chart.size() < static_cast<std::size_t>(music_chart_size)) {
        music.chart.push_back(static_cast<int>(track.number()));
      }
    }
  }
  return music;
}

json::JsonValue music_to_json(const MusicLibrary &music) {
  using json::JsonValue;
  JsonValue::Object owned;
  for (const auto &[track, state] : music.owned) {
    owned.emplace_back(std::to_string(track), JsonValue{static_cast<std::int64_t>(state)});
  }
  JsonValue::Array programs;
  for (const auto &program : music.programs) {
    JsonValue::Array tracks;
    for (const int track : program) {
      tracks.push_back(JsonValue{static_cast<std::int64_t>(track)});
    }
    programs.push_back(JsonValue{std::move(tracks)});
  }
  JsonValue::Array names;
  for (const auto &name : music.names) {
    names.push_back(JsonValue{name});
  }
  JsonValue::Array chart;
  for (const int track : music.chart) {
    chart.push_back(JsonValue{static_cast<std::int64_t>(track)});
  }
  return JsonValue{JsonValue::Object{
      {"owned", JsonValue{std::move(owned)}},
      {"programs", JsonValue{std::move(programs)}},
      {"names", JsonValue{std::move(names)}},
      {"program", JsonValue{static_cast<std::int64_t>(music.program)}},
      {"random", JsonValue{music.random}},
      {"chart", JsonValue{std::move(chart)}},
  }};
}

void roll_chart(MusicLibrary &music, const std::uint32_t seed) {
  std::mt19937 random(seed);
  std::vector<int> pool(music_chart_pool);
  std::iota(pool.begin(), pool.end(), 0);
  std::shuffle(pool.begin(), pool.end(), random);
  music.chart.assign(pool.begin(), pool.begin() + music_chart_size);
  // At least one of the first few credits makes the chart.
  if (std::ranges::none_of(music.chart, [](const int track) { return track < music_chart_hits; })) {
    std::uniform_int_distribution<int> place(0, music_chart_size - 1);
    std::uniform_int_distribution<int> hit(0, music_chart_hits - 1);
    music.chart[static_cast<std::size_t>(place(random))] = hit(random);
  }
}

} // namespace phantomcrash
