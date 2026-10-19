#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace phantomcrash {

// Native mix categories describe the event, not the sample file: the same
// recording may serve different purposes. Replay retains the event's category.
enum class AudioBus { music, weapons, robots, environment, pickups, ui, dialogue, movies, count };
inline constexpr std::size_t audio_bus_count = static_cast<std::size_t>(AudioBus::count);
inline constexpr std::array<std::string_view, audio_bus_count> audio_bus_names{
    "music", "weapons", "robots", "environment", "pickups", "interface", "dialogue", "movies"};

struct AudioOptions {
  int master{100};
  std::array<int, audio_bus_count> levels{100, 100, 100, 100, 100, 100, 100, 100};

  [[nodiscard]] float gain(AudioBus bus) const noexcept {
    const auto index = static_cast<std::size_t>(bus);
    const int level = index < levels.size() ? levels[index] : 100;
    return static_cast<float>(std::clamp(master, 0, 100) * std::clamp(level, 0, 100)) / 10000.0F;
  }
  bool operator==(const AudioOptions &) const = default;
};

} // namespace phantomcrash
