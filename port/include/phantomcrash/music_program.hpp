#pragma once

#include <cstddef>
#include <optional>
#include <random>
#include <vector>

namespace phantomcrash {

// One battle's playback order. The save owns the program; this cursor owns
// only playback progress and does not change purchased tracks or the save.
class MusicProgram {
public:
  void reset(std::vector<int> tracks, bool random);
  [[nodiscard]] std::optional<int> next();
  void reject(int track);
  [[nodiscard]] bool empty() const {
    return tracks_.empty();
  }

private:
  std::vector<int> tracks_;
  std::size_t cursor_{};
  bool random_mode_{};
  std::minstd_rand random_{std::random_device{}()};
};

} // namespace phantomcrash
