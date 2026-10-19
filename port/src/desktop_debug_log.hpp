#pragma once

#include <filesystem>

namespace phantomcrash {

// Copies every SDL_Log line, with the time since start, into `file` (emptied
// first), as well as SDL's own output, so a play session can be read back.
void start_debug_log(const std::filesystem::path &file);

} // namespace phantomcrash
