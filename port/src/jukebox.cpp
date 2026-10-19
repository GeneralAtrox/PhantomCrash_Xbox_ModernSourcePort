#include "phantomcrash/jukebox.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
namespace {

std::vector<std::string> names(const json::JsonValue &value) {
  std::vector<std::string> result;
  for (const auto &name : value.array()) {
    result.push_back(name.string());
  }
  return result;
}

bool listed(const std::vector<std::string> &screens, const std::string &screen) {
  return std::ranges::find(screens, screen) != screens.end();
}

} // namespace

JukeboxTuning parse_jukebox(const json::JsonValue &document) {
  try {
    JukeboxTuning tuning;
    tuning.playlist = std::max(1, static_cast<int>(json::required(document, "playlist").number()));
    tuning.gap_seconds = std::max(0.0, json::required(document, "gap_seconds").number());
    tuning.volume = static_cast<float>(document.number_or("volume", 1.0));
    tuning.shuffle_on = names(json::required(document, "shuffle_on"));
    tuning.plays_on = names(json::required(document, "plays_on"));
    return tuning;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("radio.json: ") + error.what());
  }
}

Jukebox::Jukebox(JukeboxTuning tuning, std::vector<std::string> songs, const std::uint32_t seed)
    : tuning_(std::move(tuning)), songs_(std::move(songs)), random_(seed == 0 ? 1U : seed) {}

std::optional<std::string> Jukebox::pick() {
  silence_ = 0.0;
  if (playlist_.empty()) {
    return std::nullopt;
  }
  std::uniform_int_distribution<std::size_t> slot(0, playlist_.size() - 1);
  return playlist_[slot(random_)];
}

std::optional<std::string> Jukebox::open(const std::string &screen) {
  if (!listed(tuning_.shuffle_on, screen)) {
    return std::nullopt;
  }
  // Distinct songs, as the original draws until it has enough (0x1bae7).
  playlist_ = songs_;
  std::ranges::shuffle(playlist_, random_);
  playlist_.resize(std::min(playlist_.size(), static_cast<std::size_t>(tuning_.playlist)));
  return pick();
}

std::optional<std::string> Jukebox::update(const std::string &screen, const double tick_seconds,
                                           const bool silent) {
  if (!silent || !listed(tuning_.plays_on, screen)) {
    silence_ = 0.0;
    return std::nullopt;
  }
  silence_ += tick_seconds;
  if (silence_ + 1e-9 < tuning_.gap_seconds) {
    return std::nullopt;
  }
  return pick();
}

} // namespace phantomcrash
