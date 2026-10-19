#pragma once
#include "phangrade_session.hpp"

namespace phangrade {
inline bool initial_fullscreen(bool requested) {
  return requested && !GetEnvironmentVariableW(L"PHANGRADE_SETTINGS", nullptr, 0);
}

// Adapter for the SDL3 desktop hosts shared by the Xbox ports. Restore normal
// placement before entering fullscreen, and keep their existing menus in sync.
template <class Host, class Menu> class DesktopSession : public Session {
public:
  DesktopSession(Host &host, Menu &menu, bool hidden) {
    if (!active() || hidden) {
      return;
    }
    menu.set_fullscreen(host.fullscreen());
    menu.set_visible(!host.fullscreen());
    attach(
        host.native_window(), [&host] { return host.fullscreen(); },
        [&host, &menu](bool enabled) {
          menu.set_fullscreen(enabled);
          if (enabled) {
            menu.set_visible(false);
          }
          host.set_fullscreen(enabled);
          if (!enabled) {
            menu.set_visible(true);
          }
        });
  }
};
} // namespace phangrade
