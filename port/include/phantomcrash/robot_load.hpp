#pragma once

// A robot's load and what it does to its movement (0x075a36,
// docs/PART_RECORDS.md). The heavier the robot for its body and legs, the
// slower it walks, dashes and turns and the lower it jumps; past its legs'
// capacity it slows sharply.

#include "phantomcrash/garage.hpp"
#include "phantomcrash/robot_mover.hpp"
#include "phantomcrash/robot_parts.hpp"

#include <vector>

namespace phantomcrash {

// The parts a robot carries, as record indices and levels (-1: none).
struct RobotLoadout {
  int chip{-1};
  int chip_level{};
  int body{-1};
  int body_level{};
  int legs{-1};
  int legs_level{};
  std::vector<PartRef> weapons;
};

// The parts fitted in one of the player's hangars, at their stock levels.
[[nodiscard]] RobotLoadout hangar_loadout(const Garage &garage, int hangar);
// The same for a hangar's fitting as it is or would be.
[[nodiscard]] RobotLoadout hangar_loadout(const Garage &garage, const Hangar &fitted);

struct LoadStats {
  float weight{};   // every part's weight at its level (0x075f93)
  float capacity{}; // the legs' capacity at their level
  // The speed (0x07c148) and jump (0x07c1c4) factors: 1 - level / 99 * 0.1
  // for the legs' level, times the square root of a frame weight over the
  // weight, times (capacity / weight)^10 when overloaded. The speed takes
  // the robot's own body and legs at level 0 as its frame, the jump body 0
  // and legs 0.
  float speed_factor{1.0F};
  float jump_factor{1.0F};
};

// Missing parts weigh nothing; a robot that weighs nothing is not slowed.
[[nodiscard]] LoadStats load_stats(const PartTables &tables, const RobotLoadout &loadout);

// What the legs manage under the load (0x07c34e), as SV SPEC shows them:
// the dash (MAX SPEED, km/h) is the legs' speed times the speed factor
// (20..2000), the walk (RUN SPEED) half of it (10..1000), and the jump
// height (m) the legs' jump times the jump factor (2.5..100). Without legs,
// all are 0.
struct LegsSpeeds {
  float walk{};
  float dash{};
  float jump{};
};
[[nodiscard]] LegsSpeeds legs_speeds(const PartTables &tables, const RobotLoadout &loadout,
                                     const LoadStats &load);

// `base` under the load (0x07c34e). The dash is the legs' speed times the
// speed factor (20..2000) and the walk half of it (10..1000); for a legs
// model whose motions move it (0x232558) each becomes the motions'
// playback rate (the value / 216 / the model's speed), which scales
// `base`'s walk or dash speeds, taken as the motions' strides
// (stride_tuning), or the table's own a frame without them. Without
// root motion the robot moves at the value / 216 a frame. The
// turns are the legs' turn times the speed factor (0.1..90 degrees a
// 60 Hz frame, two a tick) for the ground, the dash, the air and the
// upper body's twist, and the jump height is the
// legs' jump under the load, as above; legs without those curves (older
// extractions) scale `base`'s by the speed and jump factors.
[[nodiscard]] MovementTuning loaded_movement(const PartTables &tables, const RobotLoadout &loadout,
                                             const LoadStats &load, MovementTuning base);

} // namespace phantomcrash
