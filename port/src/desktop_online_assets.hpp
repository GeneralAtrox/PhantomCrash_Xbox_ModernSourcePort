#pragma once

#include "desktop_online_match.hpp"
#include "phantomcrash/robot_library.hpp"

namespace phantomcrash {
class WorldRenderer;

class DesktopMatchAssets final : public online::MatchAssets {
public:
  DesktopMatchAssets(RobotLibrary &robots, WorldRenderer &world);
  void prepare(CoopMatch &match) override;

private:
  RobotLibrary &robots_;
  WorldRenderer &world_;
};

} // namespace phantomcrash
