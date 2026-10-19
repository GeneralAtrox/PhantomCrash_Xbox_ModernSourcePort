#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"
#include "phantomcrash/motion.hpp"
#include "phantomcrash/source_suspension.hpp"
#include "phantomcrash/source_roll.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace phantomcrash {

// Robot skeletons (robots/skeletons.json, from the static trees in the XBE;
// see src/phantomcrash_research/robots.py). A skeleton does not belong to a
// model: its nodes fill bone slots, and each drawn node of the robot's part
// models is skinned rigidly to one slot (docs/ROBOTS.md).

enum class SkeletonNodeType : int {
  plain = 0,
  arm_joint = 1,
  ik_start = 2, // a leg chain: its first child is aimed (one bone) or bent
  ik_bone = 3,  // (two bones) to reach the track of the chain's end
  ik_end = 4,
  arms = 7, // the two arm skeletons hang here
};

inline constexpr std::uint32_t node_root_motion = 0x2;
inline constexpr std::uint32_t node_turns_with_aim = 0x4;
inline constexpr std::uint32_t node_pitches_with_aim = 0x100;
// The aim pitch the 0x100 nodes and the shoulder mounts follow, either way (0x0aa6a0).
inline constexpr float aim_pitch_limit = 0.4F;
inline constexpr std::uint32_t node_effect_point = 0x20000;
inline constexpr std::uint32_t node_forearm = 0x30; // 0x10 one side, 0x20 the other
inline constexpr std::uint32_t node_wheel = 0x200;  // rolls with the robot (0x0aa499)

struct SkeletonNode {
  SkeletonNodeType type{SkeletonNodeType::plain};
  std::uint32_t flags{};
  int track{-1};
  Vec3 translation{}; // as stored; the evaluator mirrors it into game space
  Vec3 rotation{};
  int bone{-1};
  std::optional<SourceSuspension> source_suspension;
  std::optional<SourceRollControl> source_roll;
  std::vector<SkeletonNode> children;
  bool source_model_root{}; // control zero of the imported leg model, including non-drawing nodes
};

struct RobotSkeletons {
  std::vector<std::optional<SkeletonNode>> legs;                // by leg type
  std::array<std::vector<std::optional<SkeletonNode>>, 2> arms; // [side][arm type]
};

[[nodiscard]] SkeletonNode parse_skeleton_node(const json::JsonValue &value);
[[nodiscard]] RobotSkeletons parse_robot_skeletons(const json::JsonValue &document);

// A motion playing at a frame, optionally fading in from the one before it.
// A motion frozen where a change caught it, and its weight against the
// layers before it.
struct MotionLayer {
  const Motion *motion{};
  float frame{};
  float weight{1.0F};
  bool keep_sway{true}; // as PoseInput::keep_sway was while it played
};
inline constexpr std::size_t motion_layers = 4;

// The root-motion track's translation in game space: what stays on the
// body, and what moves onto the feet targets.
struct RootSplit {
  Vec3 body;
  Vec3 sway;
};

struct MotionSource {
  const Motion *motion{};
  float frame{};
  // What `motion` blends from, oldest first. A change that interrupts a
  // blend freezes the whole blend here, so the pose carries on from it.
  std::array<MotionLayer, motion_layers> previous{};
  std::size_t previous_count{};
  float weight{1.0F}; // of `motion` against `previous`

  [[nodiscard]] bool blending() const noexcept {
    return motion != nullptr && previous_count > 0 && weight < 1.0F;
  }
  [[nodiscard]] TrackSample sample(int track) const;
  // Each layer split as its own keep_sway says (`keep_sway` for `motion`),
  // then blended. `bind` stands in for a missing translation; `sampled` is
  // used whole when nothing blends.
  [[nodiscard]] RootSplit root_split(int track, Vec3 bind, Vec3 sampled, bool keep_sway) const;
  // `as_matrix(layer)` of each frozen layer, blended oldest first
  // with `mix(from, to, weight)`; nothing when no layer has a motion.
  template <typename Matrix, typename AsMatrix, typename Mix>
  [[nodiscard]] std::optional<Matrix> blend_from(AsMatrix as_matrix, Mix mix) const {
    std::optional<Matrix> out;
    for (std::size_t i = 0; i < previous_count && i < previous.size(); ++i) {
      const MotionLayer &layer = previous[i];
      if (layer.motion != nullptr) {
        const Matrix next = as_matrix(layer);
        out = out ? mix(*out, next, layer.weight) : next;
      }
    }
    return out;
  }
};

inline constexpr int bone_slots = 64; // includes imported part rigs
inline constexpr int pose_parts = 3;  // 0: body and legs, 1: left arm, 2: right arm

struct PoseContact {
  Vec3 position;
  const Motion *motion{};
  std::array<std::size_t, 3> keys{};
};
[[nodiscard]] PoseContact pose_contact(const SkeletonNode &node, const Mat4 &parent,
                                       const MotionSource &source);
// Whether the contact crossed a key since `before` in the same motion: a
// step (0x0ad737 counts these in robot +0x210c).
[[nodiscard]] bool contact_stepped(const PoseContact &before, const PoseContact &now);

struct PosePart {
  std::vector<PoseContact> contacts;
  std::array<Mat4, bone_slots> bones{};
  std::array<bool, bone_slots> set{};
  std::optional<Mat4> mount;
  std::array<std::optional<Mat4>, 14> movement_mounts;
  std::optional<Mat4> source_model_root;
  std::vector<std::optional<Mat4>> source_controls;
};

using Pose = std::array<PosePart, pose_parts>;

struct ArmAim {
  float weight{};
  Vec3 target;
  float pitch_limit{0.8F};
};

struct PoseInput {
  Mat4 root;         // the robot's ground point and heading (yaw, then position)
  MotionSource legs; // drives the body-and-legs skeleton
  std::array<MotionSource, 2> arms;
  std::array<MotionSource, 2> firing_poses;
  std::array<MotionSource, 7> body_auxiliary;
  std::array<ArmAim, 2> arm_aim;
  std::array<const SkeletonNode *, 2> arm_skeletons{};
  float aim_yaw{};             // turns the nodes flagged 0x4 (the upper body)
  float aim_pitch{};           // pitches the nodes flagged 0x100, within +-0.4 rad
  float forearm_turn{};        // added about x to the forearms (node_forearm) otherwise
  float wheel_turn{};          // added about x to the wheels (node_wheel) instead of both
  double procedural_seconds{}; // independent of clip speed, looping and transitions
  const std::array<float, 64> *source_roll_angles{};
  const std::array<std::optional<Mat4>, 64> *source_roll_frames{};
  const std::vector<std::optional<Mat4>> *source_leg_locals{};
  bool keep_sway{}; // idle and walking keep the body's sideways sway
};

// Poses the body-and-legs skeleton `root` (and the arms under it) into
// world-space bone matrices, reproducing the pose builder at 0x0aa499.
void build_pose(const SkeletonNode &root, const PoseInput &input, Pose &pose);

// D3DX rotation matrices (row vectors, left-handed).
[[nodiscard]] Mat4 rotation_x(float angle);
[[nodiscard]] Mat4 rotation_y(float angle);
[[nodiscard]] Mat4 rotation_z(float angle);

} // namespace phantomcrash
