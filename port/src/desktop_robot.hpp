#pragma once

#include "desktop_world_renderer.hpp"
#include "phantomcrash/robot.hpp"
#include "phantomcrash/rigid_animation.hpp"
#include "phantomcrash/skeleton.hpp"

#include <array>
#include <optional>
#include <vector>

namespace phantomcrash {

// A robot's part models on the GPU, drawn from a pose: the body and legs
// share pose part 0 (the legs' bone slots follow the body's), each arm has
// its own part, and the shoulder weapons ride the upper body's bone (the
// node flagged 0x4; 0x0acb36 hands it to all four weapon slots).
class RobotVisual final {
public:
  RobotVisual(WorldRenderer &world, const RobotAssembly &assembly);

  [[nodiscard]] bool ready() const noexcept {
    return body_ >= 0 && legs_ >= 0;
  }

  // Appends the robot's parts, posed by `pose`, to `items`; `root` places
  // parts whose bones the pose did not reach.
  void add_items(const Pose &pose, const Mat4 &root, std::vector<WorldItem> &items,
                 const std::array<Mat4, 2> *shoulder_mounts = nullptr,
                 const std::array<bool, 4> &lost = {});
  // Where `pose` puts the upper body's bone, if it reached it.
  [[nodiscard]] std::optional<Vec3> torso(const Pose &pose) const;
  void animate(int motion, float frame, bool loop, float seconds,
               const std::array<bool, 4> &fired = {}, std::uint64_t revision = 0, float speed = 1);
  [[nodiscard]] std::optional<Mat4> source_weapon_frame(const Pose &pose, const Mat4 &root,
                                                        const std::array<Mat4, 2> &shoulder_mounts,
                                                        int slot, int node) const;
  [[nodiscard]] std::optional<Mat4> source_body_frame(const Pose &pose, const Mat4 &root,
                                                      int node) const;

private:
  struct Part {
    int model{-1};
    int pose_part{};
    int first_slot{};
    std::vector<Mat4> node_worlds;
    int paint{-1};
  };

  void add(Part &part, const Pose &pose, const Mat4 &root, std::vector<WorldItem> &items,
           int weapon_slot = -1);
  void rigid(const ModelData &model, const Mat4 &root, int weapon_slot, std::vector<Mat4> &nodes,
             int attachment_node = -1, Mat4 *attachment_frame = nullptr) const;

  WorldRenderer &world_;
  int body_{-1};
  int legs_{-1};
  int paint_{-1};
  int torso_slot_{-1};
  int emblem_{-1};
  int emblem_texture_{-1};
  std::vector<Part> parts_;
  std::vector<Part> shoulders_;
  std::array<Mat4, 2> shoulder_shifts_{}; // RobotAssembly::shoulder_shifts
  int motion_{28};
  float frame_{};
  bool loop_{true};
  RigidPlayback body_playback_;
  std::array<int, 2> source_turn_frames_{-1, -1};
  bool source_actor_idle_{true};
  std::array<float, 4> weapon_seconds_{};
  std::array<bool, 4> weapon_started_{};
  std::array<RigidPlayback, 4> weapon_playback_;
  mutable std::optional<std::array<Mat4, 2>> source_arm_mounts_;
  mutable std::optional<std::array<Mat4, 2>> source_shoulder_mounts_;
  std::array<bool, 2> source_arm_mount_valid_{};
  std::array<bool, 2> source_shoulder_mount_valid_{};
};

} // namespace phantomcrash
