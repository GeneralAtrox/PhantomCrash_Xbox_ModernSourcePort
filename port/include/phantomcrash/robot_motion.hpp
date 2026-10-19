#pragma once

namespace phantomcrash {

// Leg motions by their index in a leg motion bank, as the movement code
// starts them (0x08ddb2 and its callers). The arm banks play the same index.
namespace robot_motion {
inline constexpr int walk = 0;
inline constexpr int walk_back = 1;
inline constexpr int dash = 2;      // 3 and 4 while turning right and left
inline constexpr int dash_back = 5; // 6 and 7 while turning right and left
inline constexpr int turn_right = 8;
inline constexpr int turn_left = 9;
inline constexpr int jump = 10;
inline constexpr int air = 11;
inline constexpr int death = 12; // 0x077082: shot-down body/leg and arm motion
// Source clips 13/15 lean toward heading_right (local -X); 14/16 lean left.
// These are the rider's directions, not the model-space X axis.
inline constexpr int dodge_right = 13;
inline constexpr int dodge_left = 14;
inline constexpr int air_dodge_right = 15;
inline constexpr int air_dodge_left = 16;
// The hit reactions (0x08dcd1), by the way the hit pushes the robot: back,
// forward, to its left or its right; in the air, those 12 later.
inline constexpr int hit_back = 17;
inline constexpr int hit_forward = 18;
inline constexpr int hit_left = 19;
inline constexpr int hit_right = 20;
inline constexpr int air_hit_offset = 12;
inline constexpr int brake = 27;
inline constexpr int stand = 28;
} // namespace robot_motion

} // namespace phantomcrash
