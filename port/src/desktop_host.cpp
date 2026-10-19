#include "desktop_host.hpp"

#include "phantomcrash/version.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

[[noreturn]] void fail(const char *what) {
  throw std::runtime_error(std::string(what) + ": " + SDL_GetError());
}

// A 16:9 window filling most of the usable desktop height, so the game starts
// large and sharp; fullscreen uses the display's native mode.
void initial_window_size(int &width, int &height) {
  width = 1280;
  height = 720;
  SDL_Rect usable{};
  if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.h > 0) {
    height = std::max(480, usable.h * 5 / 6);
    width = std::min(usable.w, height * 16 / 9);
    height = std::min(height, width * 9 / 16);
  }
}

} // namespace

DesktopHost::DesktopHost(const bool fullscreen, const bool hidden,
                         const std::optional<std::pair<int, int>> size) {
  const std::string title = std::string(port_name()) + " " + std::string(port_version());
  if (!SDL_SetAppMetadata(title.c_str(), std::string(port_version()).c_str(),
                          "org.phantomcrash.port")) {
    fail("Unable to set SDL app metadata");
  }
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    fail("Unable to initialize SDL");
  }
  try {
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11,direct3d12,opengl");
    int width = 0;
    int height = 0;
    initial_window_size(width, height);
    if (size) {
      width = size->first;
      height = size->second;
    }
    // Created hidden; show() reveals it once a black frame is presented.
    const SDL_WindowFlags flags =
        SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!SDL_CreateWindowAndRenderer(title.c_str(), width, height, flags, &window_, &renderer_)) {
      fail("Unable to create the window");
    }
    SDL_SetWindowMinimumSize(window_, 320, 240);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    set_vsync(true);
    if (fullscreen) {
      set_fullscreen(true);
    }
    if (!hidden) {
      show();
    }
  } catch (...) {
    SDL_DestroyRenderer(renderer_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
    throw;
  }
}

DesktopHost::~DesktopHost() {
  SDL_DestroyRenderer(renderer_);
  SDL_DestroyWindow(window_);
  SDL_Quit();
}

void *DesktopHost::native_window() const {
  return SDL_GetPointerProperty(SDL_GetWindowProperties(window_),
                                SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
}

Viewport DesktopHost::viewport() const {
  int width = 0;
  int height = 0;
  if (!SDL_GetCurrentRenderOutputSize(renderer_, &width, &height)) {
    SDL_GetWindowSizeInPixels(window_, &width, &height);
  }
  return fit_canvas(width, height);
}

void DesktopHost::set_vsync(const bool enabled) {
  const int wanted = enabled ? 1 : 0;
  if (wanted != vsync_) {
    SDL_SetRenderVSync(renderer_, wanted);
    vsync_ = wanted;
  }
}

bool DesktopHost::fullscreen() const {
  return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
}

void DesktopHost::set_fullscreen(const bool enabled) {
  // Borderless desktop fullscreen: no mode switch, the display's own resolution.
  SDL_SetWindowFullscreenMode(window_, nullptr);
  SDL_SetWindowFullscreen(window_, enabled);
  SDL_SyncWindow(window_);
}

bool DesktopHost::focused() const {
  return (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

void DesktopHost::show() {
  if (!(SDL_GetWindowFlags(window_) & SDL_WINDOW_HIDDEN)) {
    return;
  }
  const auto blank = [&] {
    SDL_PumpEvents();
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderPresent(renderer_);
  };
  blank();
  SDL_ShowWindow(window_);
  blank();
}

} // namespace phantomcrash
