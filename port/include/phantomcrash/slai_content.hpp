#pragma once
#include "phantomcrash/settings.hpp"

namespace phantomcrash {
[[nodiscard]] bool slai_installed(const std::filesystem::path &root);
[[nodiscard]] bool legacy_slai(const std::filesystem::path &root);
[[nodiscard]] std::filesystem::path content_profile_path(const std::filesystem::path &base,
                                                         bool slai, bool legacy);
[[nodiscard]] Settings load_content_settings(const std::filesystem::path &base,
                                             const std::filesystem::path &profile, bool slai);
} // namespace phantomcrash
