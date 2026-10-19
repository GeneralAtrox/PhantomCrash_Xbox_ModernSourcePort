#pragma once

#include "phantomcrash/math3d.hpp"
#include "phantomcrash/rigid_source_playback.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct cgltf_data;

namespace phantomcrash {
struct ModelData;

struct RigidNode {
  int parent{-1};
  Mat4 local;
  std::array<float, 3> position{};
  std::array<float, 4> rotation{0, 0, 0, 1};
  std::array<float, 3> scale{1, 1, 1};
};
struct RigidChannel {
  enum class Path { position, rotation, scale };
  int node{};
  Path path{};
  bool step{};
  std::vector<float> times;
  std::vector<std::array<float, 4>> values;
};
struct RigidClip {
  std::string name;
  float duration{};
  std::vector<RigidChannel> channels;
  std::vector<SourcePmtTrack> source_tracks;
};
struct RigidRig {
  std::string prefix;
  std::vector<std::string> motion_clips;
  std::vector<int> transition_frames;
  bool preserve_disabled_clips{};
  std::vector<int> independent_motions;
  std::string weapon_clip;
  float frame_rate{60};
  std::vector<RigidNode> nodes;
  std::vector<RigidClip> clips;
  std::array<int, 2> arm_mount_nodes{-1, -1};
  std::array<int, 2> shoulder_mount_nodes{-1, -1};
  Vec3 body_mount_offset{}; // Cancel the legacy shoulder offset on a source body node.
};

// Source models can disable an action without stopping their previous clip.
// Advance once per simulation update, never from a render or attachment query.
struct RigidPlayback {
  std::string clip;
  float seconds{};
  bool loop{};
  int motion{-1};
  std::uint64_t revision{};
  std::string primary_clip;
  int transition_frames{};
  int transition_frame{};
  float transition_accumulator{};
  // Evaluated once per simulation tick; draws and socket reads share it.
  std::vector<RigidNode> nodes;
  RigidSourcePlayback source;
  float source_accumulator{};
  float source_speed{1};
  void update(const RigidRig &rig, int requested, float frame, bool repeating, float delta,
              std::uint64_t requested_revision = 0, int transition_override = -1, float speed = 1);
  void update_weapon(const RigidRig &rig, float delta, bool started, bool restart);
};

// Preserve hierarchy-only nodes as well as drawing nodes. This reads glTF
// channels as data; no original executable animation code is run.
void read_rigid_animation(const cgltf_data &data, ModelData &model, std::string prefix);
[[nodiscard]] std::vector<RigidNode> sample_rigid_pose(const RigidRig &rig, std::string_view clip,
                                                       float seconds, bool loop);
void blend_rigid_pose(std::vector<RigidNode> &from, const std::vector<RigidNode> &to, float weight);
void pose_rigid_model(const ModelData &model, std::string_view clip, float seconds, bool loop,
                      const Mat4 &root, std::vector<Mat4> &drawing_nodes,
                      std::array<Mat4, 2> *arm_mounts = nullptr, int attachment_node = -1,
                      Mat4 *attachment_frame = nullptr,
                      std::array<Mat4, 2> *shoulder_mounts = nullptr,
                      const std::vector<RigidNode> *evaluated = nullptr);
} // namespace phantomcrash
