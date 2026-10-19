#pragma once

#include "phantomcrash/robot_combat.hpp"

#include "desktop_robot.hpp"
#include "desktop_world_renderer.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/robot_shadow.hpp"
#include "phantomcrash/robot_library.hpp"
#include "phantomcrash/source_motion_actions.hpp"

#include <SDL3/SDL.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// Draws a Battle: each robot's parts posed by its own animator, which
// follows the motion its mover asks for, and the shots and blasts as
// glowing marks over the world.
class BattleView final {
public:
  BattleView(WorldRenderer &world, RobotLibrary &robots) : world_(world), robots_(robots) {}

  // Adds the look of the next battle robot. Throws std::runtime_error when
  // a part is out of range.
  void add(const RobotParts &parts);
  // Robot `index` looks like `parts` from now on (another robot has come in).
  void replace(std::size_t index, const RobotParts &parts);
  // Loads the models and textures of robots that may come in later, so that
  // replace finds them ready instead of loading them mid-match.
  void preload(const std::vector<RobotParts> &looks);
  [[nodiscard]] std::size_t size() const noexcept {
    return visuals_.size();
  }
  // `base` with robot `index`'s stride speeds (stride_tuning).
  [[nodiscard]] MovementTuning movement(std::size_t index, const MovementTuning &base) const;
  // Robot `index`'s collision radius from its legs, or `fallback`.
  [[nodiscard]] float radius(std::size_t index, float fallback) const;
  // Robot `index`'s legs model (0x05f788), or -1.
  [[nodiscard]] int legs_model(std::size_t index) const;
  // Where robot `index`'s upper body is, as last posed.
  [[nodiscard]] std::optional<Vec3> torso(std::size_t index) const;

  // How camouflaged robots look (optical_camouflage.hpp).
  void set_optical(const OpticalCamouflage &look) {
    optical_ = look;
  }
  // How the entry shield looks; its model loads here.
  void set_shield(const EntryShieldLook &look);
  // How the robots' shadows look, on a split screen or not; the flat
  // shadows' model loads here.
  void set_shadows(const RobotShadowLook &look, bool split_screen);
  // Connects the shared rigs before the simulation evaluates poses and fires.
  void bind(Battle &battle);
  // Copies simulation poses and advances the shared shield clock once per tick.
  void tick(Battle &battle, float seconds);
  [[nodiscard]] std::optional<Mat4> source_weapon_frame(const Battle &battle, int robot, int slot,
                                                        int node) const;
  [[nodiscard]] std::optional<Mat4> source_impact_frame(const Battle &battle, int robot) const;
  [[nodiscard]] SourceMotionClock source_motion_clock(std::size_t robot) const;
  [[nodiscard]] std::optional<Mat4> source_model_frame(const Battle &battle, int robot, int part,
                                                       int node) const;
  // Appends the robots' parts to `items`, all but robot `hidden`'s (the
  // one the camera sits in). `hidden_shield` suppresses only that robot's
  // shield visual during its entrance, without changing damage protection.
  void add_items(const Battle &battle, std::vector<WorldItem> &items, int hidden = -1,
                 int hidden_shield = -1);
  // Forgets the wrecks posed so far: another battle takes over.
  void clear_wrecks();

private:
  void sync_wrecks(Battle &battle);
  void add_wreck_items(const Battle &battle, std::vector<WorldItem> &items) const;
  std::vector<std::vector<WorldItem>> wreck_items_;
  std::vector<Mat4> wreck_roots_; // identifies each posed wreck across restores
  std::vector<bool> wreck_solid_;
  const Battle *wreck_battle_{};
  std::uint64_t wreck_generation_{};

  void add_shield(const Combatant &robot, std::vector<WorldItem> &items) const;
  void advance_shield(const Battle &battle, float seconds);

  struct Visual {
    RobotAssembly assembly;
    std::shared_ptr<RobotCombat> combat;
    std::unique_ptr<RobotVisual> parts;
    Pose pose{};
    int ground_node{-1}; // its flat shadow's node in the shadows' model
    bool dying{};        // presentation transition logging
    std::optional<std::uint64_t> motion_revision;
  };

  [[nodiscard]] std::unique_ptr<Visual> make_visual(const RobotParts &parts) const;
  // Marks the robot's items [first, end) as casting a volume, or adds its
  // flat shadow.
  void add_shadow(const Combatant &robot, const Visual &visual, const Mat4 &root, std::size_t first,
                  std::vector<WorldItem> &items) const;

  WorldRenderer &world_;
  RobotLibrary &robots_;
  std::vector<std::unique_ptr<Visual>> visuals_;
  OpticalCamouflage optical_;
  EntryShieldLook shield_;
  int shield_model_{-1};
  RobotShadowLook shadows_;
  bool split_screen_{};
  int ground_model_{-1};
  float shield_spin_{}; // 0x06f592's angle (global 0x988570)
  float shield_phase_{};
  float shield_scroll_{1.0F};
};

// A point of the world on a `width` x `height` picture: pixel x, y and the
// distance along the view, or nothing when it is behind the eye.
struct ScreenPoint {
  float x{}, y{}, depth{};
};
[[nodiscard]] bool project_point(const Mat4 &view_projection, Vec3 point, int width, int height,
                                 ScreenPoint &out);

} // namespace phantomcrash
