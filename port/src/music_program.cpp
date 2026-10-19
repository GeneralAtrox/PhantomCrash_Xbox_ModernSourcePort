#include "phantomcrash/music_program.hpp"

#include <utility>

namespace phantomcrash {

void MusicProgram::reset(std::vector<int> tracks, const bool random) {
  tracks_ = std::move(tracks);
  cursor_ = 0;
  random_mode_ = random;
}

std::optional<int> MusicProgram::next() {
  if (tracks_.empty()) {
    return std::nullopt;
  }
  if (random_mode_) {
    return tracks_[std::uniform_int_distribution<std::size_t>(0, tracks_.size() - 1)(random_)];
  }
  const int track = tracks_[cursor_];
  cursor_ = (cursor_ + 1) % tracks_.size();
  return track;
}

void MusicProgram::reject(const int track) {
  for (std::size_t index = 0; index < tracks_.size();) {
    if (tracks_[index] != track) {
      ++index;
      continue;
    }
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(index));
    if (index < cursor_) {
      --cursor_;
    }
  }
  if (cursor_ >= tracks_.size()) {
    cursor_ = 0;
  }
}

} // namespace phantomcrash
