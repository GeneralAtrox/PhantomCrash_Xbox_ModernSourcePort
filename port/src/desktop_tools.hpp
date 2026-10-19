#pragma once

#include "desktop_options.hpp"
#include "phantomcrash/data_roots.hpp"

namespace phantomcrash {
// One-shot tools return before loading or writing the user's settings/save.
[[nodiscard]] std::optional<int> run_desktop_tool(const Options &options, const DataRoots &roots);
int check_online_resources(const DataRoots &roots);
// Two peers on this computer play a long match; 0 when each frame fits 144 fps.
int check_online_stress(const DataRoots &roots, double game_seconds);
} // namespace phantomcrash
