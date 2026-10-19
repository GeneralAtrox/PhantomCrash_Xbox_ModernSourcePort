#pragma once

#include "phantomcrash/chase_camera.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"

#include <array>
#include <functional>
#include <vector>

namespace phantomcrash {

// The battle camera's views (camera +0, the placement table at 0x23289c
// run by 0x07fea8). The names are researcher-assigned.
namespace camera_view {
inline constexpr int first_person = 0; // 0x07e901, from the cockpit
inline constexpr int behind = 1;       // 0x07edfa: the port's chase camera
inline constexpr int right_side = 2;   // 0x07eb61
inline constexpr int left_side = 3;    // 0x07ec2f
inline constexpr int front = 4;        // 0x07ed12, looking back at the robot
inline constexpr int fixed = 5;        // 0x07f641, the stage's fixed cameras
inline constexpr int count = 6;
} // namespace camera_view

// A side or front view: the eye stands at `offset` from the robot's feet in
// the camera yaw's frame (+x the robot's left, -z ahead of it), and rises
// with the aim pitch when `pitch_lift` is set.
struct OrbitView {
  Vec3 offset;
  bool pitch_lift{};
};

// data/world/training.json "camera_views". The defaults are the original's.
struct CameraViewSettings {
  // Camera +0x14 + view x 4, whole degrees: 52 for every view (0x07e10c).
  std::array<float, camera_view::count> fov_degrees{52.0F, 52.0F, 52.0F, 52.0F, 52.0F, 52.0F};
  int start_view{camera_view::behind}; // the save's +0x6048 (0x07fc04)
  float tick_rate{30.0F};              // the battle's logic rate
  float blend_ticks{20.0F};            // first person <-> behind (0x0774ad)
  float follow{0.3F};                  // the yaw and pitch ease a tick (0x07fc5e, 0x07fd72)
  float pitch_limit_degrees{45.0F};
  float pitch_lift{2.0F}; // 0x2327c4: height per radian of pitch
  // Views 2-4 (0x2327c8..0x2327e8).
  std::array<OrbitView, 3> orbits{
      {{{-5.5F, 1.65F, 0.0F}, false}, {{8.0F, 5.0F, 0.0F}, true}, {{0.0F, 1.65F, -8.0F}, true}}};
  // First person: the eye's height above and distance ahead of the torso,
  // by legs model (0x2327f0, 0x232848), and how far ahead it looks.
  std::vector<float> eye_height{0.8F, 0.9F, 0.8F, 1.0F, 1.1F, 0.9F, 1.1F, 1.1F,
                                1.0F, 1.0F, 0.8F, 0.4F, 0.4F, 0.4F, 0.4F, 0.4F};
  std::vector<float> eye_forward{1.0F, 1.0F, 1.0F, 1.6F, 1.5F, 1.2F, 1.2F, 1.2F,
                                 0.9F, 1.0F, 1.0F, 1.7F, 1.8F, 1.7F, 1.8F, 1.7F};
  float look_distance{24.0F};
  // Fixed cameras: every 30 ticks the nearest of the 3 points nearest the
  // robot that sees it (camera +0xc).
  float fixed_pick_ticks{30.0F};
  int fixed_candidates{3};
};

[[nodiscard]] CameraViewSettings parse_camera_views(const json::JsonValue &value);

// The converter's tables/battle_scenes.json "fixed_cameras": a stage's
// points (0x232ae0).
[[nodiscard]] std::vector<Vec3> parse_fixed_cameras(const json::JsonValue &document, int stage);

// The view buttons (robot +0x2f6c bits 0x400..0x2000, set by 0x0774ad).
enum class ViewChange { first_person, behind, next, previous };

// Whether a camera at `from` sees `to`.
using SightTest = std::function<bool(Vec3 from, Vec3 to)>;

class CameraViews final {
public:
  CameraViews(const CameraViewSettings &settings, std::vector<Vec3> fixed_points);

  // First person and behind blend over blend_ticks; the orbit views cut.
  // Nothing changes while a blend runs. Returns whether the view changed.
  bool change(ViewChange change);
  // Follows `subject`; `chase` is the behind view. `snap` skips the ease.
  void tick(const CameraSubject &subject, const CameraShot &chase, float seconds,
            const SightTest &sees, bool snap = false);

  [[nodiscard]] int view() const noexcept {
    return view_;
  }
  // The eased yaw the views turn by.
  [[nodiscard]] float yaw() const noexcept {
    return yaw_;
  }
  // The view the camera shows or is blending to.
  [[nodiscard]] bool first_person() const noexcept {
    return view_ == camera_view::first_person;
  }
  // How much of the shot is the first-person view's: 1 once a change to
  // it has blended in, falling to 0 as a change away blends out.
  [[nodiscard]] float first_person_share() const noexcept;
  // Where the camera stands, blended while a change runs.
  [[nodiscard]] const CameraShot &shot() const noexcept {
    return shot_;
  }
  [[nodiscard]] const std::vector<Vec3> &fixed_points() const noexcept {
    return fixed_points_;
  }

private:
  [[nodiscard]] CameraShot place(int view, const CameraSubject &subject,
                                 const CameraShot &chase) const;
  void pick_fixed(const CameraSubject &subject, const SightTest &sees);

  CameraViewSettings settings_;
  std::vector<Vec3> fixed_points_;
  int view_{camera_view::behind};
  int from_{camera_view::behind};
  float blend_left_{}; // ticks
  float yaw_{};
  float pitch_{};
  float pick_left_{}; // ticks to the next fixed-camera pick
  int fixed_point_{-1};
  CameraShot shot_;
};

} // namespace phantomcrash
