#include "desktop_tools.hpp"
#include "desktop_robot_shot.hpp"

namespace phantomcrash {

std::optional<int> run_desktop_tool(const Options &options, const DataRoots &roots) {
  if (options.online_resource_check) {
    return check_online_resources(roots);
  }
  if (options.online_stress_seconds > 0.0) {
    return check_online_stress(roots, options.online_stress_seconds);
  }
  if (options.robot_shot) {
    render_robot_shot(*options.robot_shot, roots);
    return 0;
  }
  return std::nullopt;
}

} // namespace phantomcrash
