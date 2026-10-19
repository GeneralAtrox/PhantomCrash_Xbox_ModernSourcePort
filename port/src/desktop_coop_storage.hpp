#pragma once
#include "desktop_coop_state.hpp"
#include "desktop_options.hpp"

namespace phantomcrash {
[[nodiscard]] std::shared_ptr<CoopState> make_coop_state(const Options &options,
                                                         const Settings &saved,
                                                         const std::filesystem::path &preferences,
                                                         const std::filesystem::path &root,
                                                         bool smoke);
void copy_coop_profiles(const CoopState &coop, Settings &settings);
} // namespace phantomcrash
