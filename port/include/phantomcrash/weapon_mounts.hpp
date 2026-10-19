#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"
#include <map>
#include <string>
#include <vector>

namespace phantomcrash {
struct WeaponSocket {
  Mat4 local; // point and drawing node's own transform, for skinned arms
  Mat4 model; // complete static parent chain, for shoulders
  int bone{-1};
};
using WeaponMounts = std::map<std::string, std::vector<WeaponSocket>>;
[[nodiscard]] WeaponMounts parse_weapon_mounts(const json::JsonValue &value);

struct CombatAnimation {
  float raise_seconds{5.0F / 30.0F};
  float fade_frames{6.0F};
  float aim_distance{64.0F};
  float joint_pitch_limit{0.8F};
  std::array<int, 2> arm_poses{21, 22};
  float auxiliary_rate{60.0F}; // body-bank frames per second, independent of leg speed
  float auxiliary_release_turn{14.0F};
  float auxiliary_loop_end{44.0F};
  float auxiliary_loop_length{30.0F};
  float auxiliary_last{59.0F};
  float auxiliary_fire_rate{0.5F};
};
[[nodiscard]] CombatAnimation parse_combat_animation(const json::JsonValue &value);
// Rigid transform interpolation used by the original combat pose overlay.
[[nodiscard]] Mat4 blend_pose_transform(const Mat4 &from, const Mat4 &to, float weight);
[[nodiscard]] Mat4 aim_pose_transform(const Mat4 &pose, Vec3 target, Vec3 up);
} // namespace phantomcrash
