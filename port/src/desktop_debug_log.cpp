#include "desktop_debug_log.hpp"

#include <SDL3/SDL.h>

#include <cstdio>

#ifdef _WIN32
#include <share.h>
#endif

namespace phantomcrash {

namespace {

std::FILE *log_file = nullptr;
SDL_LogOutputFunction sdl_output = nullptr;
void *sdl_output_data = nullptr;

void write_line(void *data, const int category, const SDL_LogPriority priority,
                const char *message) {
  if (log_file != nullptr) {
    std::fprintf(log_file, "%10.3f %s\n", static_cast<double>(SDL_GetTicks()) / 1000.0, message);
    std::fflush(log_file);
  }
  if (sdl_output != nullptr) {
    sdl_output(data, category, priority, message);
  }
}

} // namespace

void start_debug_log(const std::filesystem::path &file) {
  if (log_file != nullptr) {
    return;
  }
#ifdef _WIN32
  // Other processes may read the log while the game writes it (Phangrade sends
  // new lines to co-op partners); _wfopen_s would open it exclusively.
  log_file = _wfsopen(file.c_str(), L"w", _SH_DENYWR);
#else
  log_file = std::fopen(file.c_str(), "w");
#endif
  if (log_file == nullptr) {
    return;
  }
  SDL_GetLogOutputFunction(&sdl_output, &sdl_output_data);
  SDL_SetLogOutputFunction(write_line, sdl_output_data);
}

} // namespace phantomcrash
