#pragma once

#include "phantomcrash/root_motion.hpp"

#include "phantomcrash/weapon_mounts.hpp"
#include "phantomcrash/movement_mounts.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/motion.hpp"
#include "phantomcrash/robot.hpp"
#include "phantomcrash/robot_mover.hpp"
#include "phantomcrash/skeleton.hpp"
#include "phantomcrash/source_leg_playback.hpp"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace phantomcrash {

// The converted robot tables, skeletons and motion banks, loaded once from
// the asset roots (so a mod can replace any of them).
class RobotLibrary final {
public:
  [[nodiscard]] const WeaponMounts &weapon_mounts() const {
    return mounts_;
  }
  [[nodiscard]] const CombatAnimation &combat_animation() const {
    return combat_;
  }
  [[nodiscard]] const MovementMounts &movement_mounts() const {
    return movement_;
  }
  explicit RobotLibrary(const DataRoots &roots);

  // False when the robots have not been converted (see errors()).
  [[nodiscard]] bool available() const noexcept {
    return available_;
  }
  [[nodiscard]] const RobotCatalogue &catalogue() const noexcept {
    return catalogue_;
  }
  [[nodiscard]] const RobotSkeletons &skeletons() const noexcept {
    return skeletons_;
  }
  // The bank at `asset_path`, loaded on first use; null when it cannot load.
  [[nodiscard]] const MotionBank *bank(const std::string &asset_path);
  // What failed to load, oldest first.
  [[nodiscard]] const std::vector<std::string> &errors() const noexcept {
    return errors_;
  }

private:
  WeaponMounts mounts_;
  MovementMounts movement_;
  CombatAnimation combat_;
  const DataRoots &roots_;
  bool available_{};
  RobotCatalogue catalogue_;
  RobotSkeletons skeletons_;
  std::map<std::string, std::unique_ptr<MotionBank>> banks_;
  std::vector<std::string> errors_;
};

// Plays one robot's motions: the leg bank drives the body and legs, the two
// arm banks play the same motion id beside it (0x08ddb2), and a change of
// motion fades over a few frames.
class RobotAnimator final {
  friend class StateInspector;

public:
  RobotAnimator(RobotLibrary &library, const RobotAssembly &assembly);
  // Copies playback state while binding to the destination pose's assembly.
  RobotAnimator(const RobotAnimator &source, const RobotAssembly &assembly);

  // Starts `motion` (a bank index) at `start_frame`, looping or holding its
  // last frame. Set restart for a fresh action even when its clip is unchanged.
  void play(int motion, bool loop, float fade_frames = 8.0F, float start_frame = 0.0F,
            bool restart = false);
  // Plays on past the loop to the last frame (a walk's stop, a landing).
  void release() noexcept {
    loop_ = false;
  }
  // Frames advance `speed` times the motion rate (the walk slows with a
  // half-pressed stick, 0x08e6b6).
  void set_speed(float speed) noexcept {
    speed_ = speed;
  }
  void tick(double seconds);
  void reset_procedural_clock() noexcept {
    procedural_seconds_ = 0;
    source_roll_ = {};
    if (assembly_.skeleton && !assembly_.source_transition_frames.empty()) {
      source_legs_.reset(*assembly_.skeleton);
    }
  }
  void advance_source_roll(const RobotMover &mover, float seconds);
  void set_auxiliary_frames(const std::array<float, 3> &frames) {
    auxiliary_frames_ = frames;
  }
  // Whether the motion keeps its sideways sway on the body (PoseInput);
  // a motion frozen by play() keeps the value it had.
  void set_keep_sway(bool keep) noexcept {
    keep_sway_ = keep;
  }
  void set_wheel_turn(float turn) noexcept {
    wheel_turn_ = turn;
  }

  [[nodiscard]] int motion() const noexcept {
    return motion_;
  }
  [[nodiscard]] bool finished() const noexcept;
  [[nodiscard]] bool looping() const noexcept {
    return loop_;
  }
  [[nodiscard]] float frame() const noexcept {
    return frame_;
  }
  [[nodiscard]] RootMotionSource root_motion_source(std::optional<std::uint64_t> revision) const {
    // S.L.A.I. has its own source playback and movement path.
    return {assembly_.source_transition_frames.empty() ? legs_ : nullptr, motion_, frame_, revision,
            assembly_.skeleton ? root_motion_track(*assembly_.skeleton) : -1};
  }

  // Poses the robot standing at `root` (its ground point and heading).
  void pose(const Mat4 &root, float aim_yaw, float aim_pitch, bool keep_sway, Pose &out,
            const std::array<ArmAim, 2> &arms = {},
            const std::array<int, 2> &firing_poses = {21, 22}) const;

private:
  [[nodiscard]] MotionTiming timing(int motion) const;
  [[nodiscard]] MotionSource source(const MotionBank *bank) const;

  const RobotAssembly &assembly_; // outlives the animator
  const MotionBank *legs_{};
  const MotionBank *body_{};
  std::array<float, 3> auxiliary_frames_{};
  std::array<const MotionBank *, 2> arms_{};
  int motion_{-1};
  bool loop_{};
  float frame_{};
  struct Layer {
    int motion{-1};
    float frame{};
    float weight{1.0F};
    bool keep_sway{true};
  };
  bool keep_sway_{true};
  float wheel_turn_{};
  double procedural_seconds_{};
  SourceRollState source_roll_;
  SourceLegPlayback source_legs_;
  bool source_transition_{};
  bool source_actor_idle_{true};
  bool has_source_roll_{};
  std::array<Layer, motion_layers> previous_{}; // what the motion blends from (MotionSource)
  std::size_t previous_count_{};
  float speed_{1.0F};
  float fade_{1.0F}; // 0..1 weight of the current motion
  float fade_frames_{1.0F};
};

// The playback frame rate of motion key frames.
inline constexpr float motion_frames_per_second = 60.0F;

// Units per second the body travels over `motion`'s loop: the root-motion
// track (the skeleton node flagged 0x2) that moves robots in the original
// (0x0910d4). 0 when the legs have no root motion (hovering drones).
[[nodiscard]] float stride_speed(const MotionBank &bank, const RobotAssembly &assembly, int motion);

// `base` with the walk and dash speeds the robot's leg motions stride at,
// so its feet stay planted and each leg part keeps its own pace; speeds the
// motions do not give stay as `base` has them.
[[nodiscard]] MovementTuning stride_tuning(RobotLibrary &library, const RobotAssembly &assembly,
                                           MovementTuning base);

} // namespace phantomcrash
