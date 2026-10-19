#pragma once

#include "phantomcrash/robot_library.hpp"
#include <array>
#include <optional>

namespace phantomcrash {
// Simulation-owned pose and firing attachments. Render views only sample it.
class RobotCombat final {
  friend class StateInspector;

public:
  RobotCombat(RobotLibrary &library, RobotAssembly assembly);
  RobotCombat(const RobotCombat &source);
  void tick(const RobotMover &mover, const std::array<bool, 4> &firing,
            const std::array<bool, 4> &lost, const std::array<Vec3, 4> &targets, float seconds,
            const std::array<int, 4> &aim_modes = {-1, -1, -1, -1}, bool piloted = false);
  void reset();
  [[nodiscard]] const RobotAssembly &assembly() const {
    return assembly_;
  }
  [[nodiscard]] bool death_finished() const {
    return animator_.motion() == robot_motion::death && animator_.finished();
  }
  [[nodiscard]] const Pose &pose() const {
    return pose_;
  }
  [[nodiscard]] const std::array<Mat4, 2> &shoulders() const {
    return shoulders_;
  }
  [[nodiscard]] std::optional<Mat4> muzzle(std::size_t slot) const;
  void advance_muzzle(std::size_t slot);
  [[nodiscard]] std::optional<MovementPoint> movement_point(std::size_t slot) const;
  [[nodiscard]] const MovementGroups &movement_groups() const {
    return movement_groups_;
  }
  [[nodiscard]] bool ready(std::size_t slot) const;
  [[nodiscard]] float aim_distance() const {
    return tuning_.aim_distance;
  }
  [[nodiscard]] float arm_weight(std::size_t side) const {
    return weights_[side];
  }
  [[nodiscard]] float motion_frame() const {
    return animator_.frame();
  }
  [[nodiscard]] int motion() const {
    return animator_.motion();
  }
  [[nodiscard]] RootMotionSource root_motion_source() const {
    return animator_.root_motion_source(motion_revision_);
  }

private:
  void advance_auxiliary(const RobotMover &mover, const std::array<bool, 4> &firing, bool piloted,
                         float seconds);
  void advance_wheels(const RobotMover &mover, float seconds);
  std::array<MovementMount, movement_slots> movement_mounts_;
  MovementGroups movement_groups_;
  RobotAssembly assembly_;
  RobotAnimator animator_;
  std::optional<std::uint64_t> motion_revision_;
  CombatAnimation tuning_;
  std::array<std::vector<WeaponSocket>, 4> sockets_;
  std::array<std::vector<Mat4>, 4> muzzles_;
  std::array<std::size_t, 4> barrel_{};
  std::array<float, 2> weights_{};
  std::array<float, 3> auxiliary_{}; // robot +0x2520, +0x2524, +0x2528
  float wheel_turn_{};               // robot +0x2590, negated (the port's sense)
  std::array<Mat4, 2> shoulders_;
  Pose pose_{};
};
} // namespace phantomcrash
