#pragma once

#include "phantomcrash/canvas.hpp"

#include <SDL3/SDL.h>

#include <optional>
#include <utility>

namespace phantomcrash {

// The SDL window and its Direct3D 11 renderer (after the Lodoss host). The
// renderer draws at the window's full pixel resolution; the canvas viewport
// maps the original 640x480 coordinates onto it.
class DesktopHost final {
public:
  // `size` is the window client size in pixels; by default a large 16:9 window.
  DesktopHost(bool fullscreen, bool hidden, std::optional<std::pair<int, int>> size = {});
  ~DesktopHost();
  DesktopHost(const DesktopHost &) = delete;
  DesktopHost &operator=(const DesktopHost &) = delete;

  [[nodiscard]] SDL_Window *window() const {
    return window_;
  }
  [[nodiscard]] SDL_Renderer *renderer() const {
    return renderer_;
  }
  // Win32 HWND of the window, or null elsewhere.
  [[nodiscard]] void *native_window() const;

  // The canvas fitted to the current output size.
  [[nodiscard]] Viewport viewport() const;

  void set_vsync(bool enabled);
  [[nodiscard]] bool fullscreen() const;
  void set_fullscreen(bool enabled);
  [[nodiscard]] bool focused() const;
  // Reveals the hidden-created window after presenting a black frame, so the
  // first thing shown is never an unpainted client area.
  void show();

private:
  SDL_Window *window_{};
  SDL_Renderer *renderer_{};
  int vsync_{-1};
};

} // namespace phantomcrash
