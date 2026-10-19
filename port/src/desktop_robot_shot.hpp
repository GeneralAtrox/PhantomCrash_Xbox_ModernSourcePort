#pragma once

#include "desktop_options.hpp"
#include "phantomcrash/data_roots.hpp"

namespace phantomcrash {

// --render-robot: one garage robot record standing alone, rendered on a
// transparent background to a square PNG (Options::RobotShot). Throws
// std::runtime_error when the robot or the 3D cannot be had.
void render_robot_shot(const Options::RobotShot &shot, const DataRoots &roots);

} // namespace phantomcrash
