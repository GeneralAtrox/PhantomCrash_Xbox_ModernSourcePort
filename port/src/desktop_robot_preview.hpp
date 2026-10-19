#pragma once

#include "phantomcrash/canvas.hpp"
#include "phantomcrash/robot_preview.hpp"

#include <SDL3/SDL.h>

#include <memory>
#include <optional>
#include <string>

namespace phantomcrash {

class DrawList;
class RobotLibrary;
class WorldRenderer;

// The 3D the menus draw with, made when a 3D screen first needs it; either
// is null when it cannot be had.
struct PreviewWorld {
  WorldRenderer *world{};
  RobotLibrary *robots{};
};

// A menu's robot preview (RobotPreviewScript): the robot shown standing, or
// its parts one by one, drawn over the screen's backdrop through the 3D
// world renderer, in the 4:3 picture.
class RobotPreview final {
public:
  // `screen` names the screen's entry in robot_preview.json; with none, or
  // without the 3D, nothing is drawn.
  RobotPreview(PreviewWorld world, const RobotPreviewScript &script, const std::string &screen);
  ~RobotPreview();
  RobotPreview(const RobotPreview &) = delete;
  RobotPreview &operator=(const RobotPreview &) = delete;

  // Shows `robot`, building it anew only when it changed.
  void show(const PreviewRobot &robot);
  void hide();
  // Stands the robot at `place` rather than the screen's (the hangar's
  // wheel, 0x01d8ba); `spins` turns it at the screen's spin from
  // place.yaw, starting over each time it starts to spin.
  void place(const PreviewPlacement &place, bool spins);
  void tick(double seconds);
  // Draws the picture and its part_clip `pixels` canvas pixels higher (the
  // window it sits in moved up).
  void set_lift(const float pixels) {
    lift_ = pixels;
  }
  // Adds the preview to `list` at the script's layer.
  void draw(DrawList &list, const Viewport &viewport, SDL_Renderer *renderer) const;

private:
  struct Model; // the robot's models and pose (desktop_robot_preview.cpp)

  void render(const Viewport &viewport, SDL_Renderer *renderer) const;

  PreviewWorld world_;
  const RobotPreviewScript &script_;
  std::optional<RobotPreviewScreen> screen_;
  float spin_{}; // the screen's own
  float lift_{};
  bool spinning_{true};
  std::optional<PreviewRobot> shown_;
  std::unique_ptr<Model> model_;
};

} // namespace phantomcrash
