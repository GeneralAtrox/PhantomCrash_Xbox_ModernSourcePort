#pragma once

#include <cstdint>
#include <optional>

namespace phantomcrash {
struct Motion;
struct MotionBank;
struct MotionTiming;
struct SkeletonNode;
[[nodiscard]] int root_motion_track(const SkeletonNode &node);

// The simulation pose's completed tick, used to sample the next locomotion
// displacement before collision. The bank belongs to RobotLibrary.
struct RootMotionSource {
  const MotionBank *bank{};
  int motion{-1};
  float frame{};
  std::optional<std::uint64_t> revision;
  int track{}; // extracted skeleton node flagged 0x2; not the body track
};

// Forward travel in model units. Walk/dash discard sideways root sway,
// as 0x090f8a does; 0x0910d4 adds the two pieces across a loop boundary.
[[nodiscard]] std::optional<float> root_motion_step(const Motion &motion,
                                                    const MotionTiming &timing, float from,
                                                    float frames, bool loop, int track = 0);
} // namespace phantomcrash
