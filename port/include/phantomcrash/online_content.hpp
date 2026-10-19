#pragma once
#include "phantomcrash/online_room.hpp"
#include <filesystem>

namespace phantomcrash::online {
// Strict initial compatibility: executable, data, all standard asset roots and
// resolved model buffers. Includes cosmetic assets; excludes profiles/settings.
[[nodiscard]] Compatibility content_fingerprint(const std::filesystem::path &executable,
                                                const std::filesystem::path &root,
                                                std::optional<bool> slai = std::nullopt);
} // namespace phantomcrash::online
