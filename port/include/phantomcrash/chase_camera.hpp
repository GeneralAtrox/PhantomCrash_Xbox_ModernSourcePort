#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"
#include "phantomcrash/stage_collision.hpp"

#include <optional>

namespace phantomcrash {

// The battle camera's behind view (0x07edfa, ticked by 0x07dd2a): eased
// after the aim, above and behind the torso, looking past the robot along
// the aim. Values from data/world/training.json "chase_camera"; the
// defaults are the original's.
struct ChaseCameraSettings {
  float fov_degrees{52.0F}; // camera +0x14 (0x09ec17)
  float near_plane{0.3F};
  float far_plane{6000.0F};
  float tick_rate{30.0F};           // the camera's ticks a second
  float follow{0.3F};               // of the aim's turn and pitch a tick (0x07fc5e, 0x07fd72)
  float pitch_limit_degrees{45.0F}; // the pitch the camera follows (0x07fd72)
  float behind{6.15F};              // eye behind the torso (0x2327dc)
  float look_ahead{8.0F};           // look point ahead of the feet (0x07edfa)
  float height{2.0F};               // eye and look point lift (0x2327c4) ...
  float pitch_lift{2.0F};           // ... plus this per radian of pitch either way
  float eye_follow_ticks{3.0F};     // the eye closes 1/this of its gap a tick (0x0805a0)
  float wall_margin{1.0F};          // off a wall the sight line meets (0x17ac40)
  float robot_clearance{1.0F};      // beyond the leg radius before the eye rises (0x17ac40)
  float clearance_lift{0.1F};       // rise a tick while that close (0x17ac38)
  float eye_radius{1.0F};           // the eye's sphere against the stage (0x052c1f)
  // Robot +0x3240, the sight line's start above the feet and the eye's
  // lift in the air. Its source is not recovered: battle robot_height.
  float robot_top{4.4F};
};

[[nodiscard]] ChaseCameraSettings parse_chase_camera(const json::JsonValue &value);

// Where a camera stands and looks, its vertical field of view and its roll
// (radians about the line of sight).
struct CameraShot {
  Vec3 eye;
  Vec3 target;
  float fov_degrees{52.0F};
  float roll{};
};

// The robot a battle camera follows.
struct CameraSubject {
  Vec3 position;       // its feet (robot +0xb0)
  float yaw{};         // heading plus twist
  float pitch{};       // the aim's
  float height{};      // one leg radius (robot +0x14)
  Vec3 torso;          // the torso node (camera +0xa0)
  int legs_model{};    // picks the first-person eye
  bool grounded{true}; // robot +0x252c
};

class ChaseCamera final {
public:
  explicit ChaseCamera(const ChaseCameraSettings &settings) : settings_(settings) {}

  // Follows `subject`, keeping the eye on this side of the active stage's
  // walls. `snap` puts it straight in place.
  void tick(const CameraSubject &subject, const StageCollision &stage, float seconds,
            bool snap = false);

  // The point above the robot the sight line to the eye starts from.
  [[nodiscard]] Vec3 anchor() const noexcept {
    return anchor_;
  }

  // Something solid at `hit` between `from` and the eye: the eye comes
  // in front of it, wall_margin nearer `from`.
  void block_eye(Vec3 from, Vec3 hit);
  // A scripted shot (battle_scene.hpp) in place of the chase view; nullopt
  // gives the view back. The chase view keeps following underneath.
  void set_shot(std::optional<CameraShot> shot) {
    shot_ = shot;
  }
  // The chase view, whatever shot is shown.
  [[nodiscard]] CameraShot chase() const noexcept {
    return {eye_, target_, settings_.fov_degrees, 0.0F};
  }
  [[nodiscard]] Vec3 eye() const noexcept {
    return shot_ ? shot_->eye : eye_;
  }
  [[nodiscard]] Vec3 target() const noexcept {
    return shot_ ? shot_->target : target_;
  }
  [[nodiscard]] Mat4 view() const;
  [[nodiscard]] Mat4 projection(float aspect) const;

private:
  ChaseCameraSettings settings_;
  float yaw_{};
  float pitch_{};
  Vec3 eye_{0.0F, 5.0F, 10.0F};
  Vec3 target_{};
  Vec3 anchor_{};
  std::optional<CameraShot> shot_;
};

} // namespace phantomcrash
