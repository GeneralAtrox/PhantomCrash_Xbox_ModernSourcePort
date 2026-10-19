#pragma once

#include "desktop_world_renderer.hpp"
#include "phantomcrash/robot_wrecks.hpp"

namespace phantomcrash {

// CPU triangle extraction from the renderer's immutable loaded model data.
// The geometry/filtering is shared with offline wrecks; no draw is submitted.
[[nodiscard]] StageCollision posed_collision(const WorldRenderer &world,
                                             const std::vector<WorldItem> &items);
[[nodiscard]] StageCollision build_wreck_collision(WorldRenderer &world, const RobotWreck &wreck);

} // namespace phantomcrash
