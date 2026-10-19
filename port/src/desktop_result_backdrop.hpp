#pragma once

#include "desktop_replay_archive.hpp"
#include "desktop_screen.hpp"
#include "desktop_world_renderer.hpp"
#include "phantomcrash/battle_camera.hpp"
#include "phantomcrash/chase_camera.hpp"

#include <memory>
#include <random>

namespace phantomcrash {
// A battle that goes on behind the receipt: the original's fresh battle of
// the contest's robots (TrainingScreen::start_backdrop).
class BackdropBattle {
public:
  BackdropBattle() = default;
  BackdropBattle(const BackdropBattle &) = delete;
  BackdropBattle &operator=(const BackdropBattle &) = delete;
  virtual ~BackdropBattle() = default;

  virtual void tick_backdrop(float seconds) = 0;
  // Each battle robot, as a replay records it (alive, in play, where).
  [[nodiscard]] virtual std::vector<ReplaySubject> backdrop_subjects() const = 0;
  [[nodiscard]] virtual const StageCollision &backdrop_collision() const = 0;
  [[nodiscard]] virtual ChaseCameraSettings backdrop_chase() const = 0;
  [[nodiscard]] virtual CameraViewSettings backdrop_views() const = 0;
  // The stage's fixed cameras (tables/battle_scenes.json).
  [[nodiscard]] virtual std::vector<Vec3> backdrop_fixed_points() const = 0;
  // The world as `camera` sees it, without the HUD.
  [[nodiscard]] virtual WorldFrame backdrop_frame(const ChaseCamera &camera, int width,
                                                  int height) = 0;
};

// The receipt's moving background. The original's RESULT (0x094fee) starts
// a fresh battle of the contest's robots behind it, and 0x04a78c watches one
// through 0x07e538: the stage's fixed cameras (the battle's view 5, 0x07f641)
// with the eye brought within 12.5 of the robot. It picks another standing
// robot every 600 frames, or when that one is down: (current + 1 + rand % 5)
// % 6.
// The port runs that battle when the match was a RUMBLING contest
// (adopt); otherwise it loops the match's own replay with the same camera
// rule, and without a replay it shows the battle's last view.
class ResultBackdrop final {
public:
  ResultBackdrop(std::shared_ptr<ReplayArchive> tape, WorldFrame still);
  // From now on the backdrop is `battle`'s.
  void adopt(std::unique_ptr<BackdropBattle> battle);
  void tick(double seconds);
  void draw(WorldRenderer &world, const Viewport &viewport);

private:
  [[nodiscard]] bool watchable(int subject) const;
  void pick();
  void follow(const StageCollision &collision, float seconds);
  void tick_tape(double seconds);
  void draw_battle(WorldRenderer &world, const Viewport &viewport);

  std::unique_ptr<BackdropBattle> battle_;
  std::shared_ptr<ReplayArchive> tape_;
  WorldFrame still_;
  ReplayFrame frame_;
  std::optional<ChaseCamera> chase_;
  CameraViewSettings view_settings_;
  std::vector<Vec3> fixed_points_;
  std::optional<CameraViews> views_; // fresh for each robot watched
  CameraShot shot_;
  std::size_t index_{};
  double time_{};
  float watched_{}; // seconds on this subject
  int subject_{-1};
  bool snap_{true};
  bool framed_{}; // the battle's camera has followed someone
  std::minstd_rand random_{0x5eed};
};
} // namespace phantomcrash
