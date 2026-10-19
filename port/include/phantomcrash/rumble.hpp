#pragma once

// Controller vibration. The original rumbles a pad only when the robot it
// pilots is damaged (0x07675e): that frame's damage, clamped, scales a
// three-step pattern (0x232678) that the effect player (0x0df810,
// 0x0dec20) walks one step at a time, a step being a number of frames at
// two motor speeds; the input poll (0x0dff50) hands the speeds to
// XInputSetState. With the vibration option off the pattern still runs on
// silent motors. Stopping the robot's effect (0x0debc0) silences the pad at
// once: pausing, being shot down.

#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace phantomcrash {

// One step: how long it lasts, and the heavy (low-frequency, the original's
// left) and light (high-frequency, right) motors as fractions of full speed.
struct RumbleStep {
  float seconds{};
  float low{};
  float high{};

  bool operator==(const RumbleStep &) const = default;
};

struct RumbleTuning {
  std::vector<RumbleStep> damage_steps; // at full strength
  // Durations scale by `short_scale` when the damage is below
  // `short_below`; speeds by the damage over `full_damage`, the damage first
  // clamped to [damage_floor, damage_ceiling].
  float damage_floor{};
  float damage_ceiling{};
  float full_damage{1.0F};
  float short_below{};
  float short_scale{1.0F};
};

// Throws std::runtime_error naming the entry at fault.
[[nodiscard]] RumbleTuning parse_rumble(const json::JsonValue &document);

// The pattern for a robot that just took `damage` (0x07675e).
[[nodiscard]] std::vector<RumbleStep> damage_rumble(const RumbleTuning &tuning, float damage);

// Both motors of a pad, 0..1.
struct RumbleMotors {
  float low{};
  float high{};

  bool operator==(const RumbleMotors &) const = default;
};

// The robot a controller port pilots took `damage` this tick.
struct DamageRumble {
  int port{};
  float damage{};
};

// The effect player: each port walks its latest pattern. A new pattern
// takes over from the one running (0x0decb0).
class RumblePlayer {
public:
  void start(int port, std::vector<RumbleStep> steps);
  void update(float seconds);
  void stop();

  [[nodiscard]] RumbleMotors motors(int port) const;
  [[nodiscard]] std::array<RumbleMotors, controller_ports> all_motors() const;

private:
  struct Track {
    std::vector<RumbleStep> steps;
    std::size_t step{};
    float left{};
  };
  std::array<Track, controller_ports> tracks_{};
};

} // namespace phantomcrash
