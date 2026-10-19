#pragma once

#ifdef _WIN32
#include <windows.h>

namespace phantomcrash {

// The menu bar's copy icon, next to Settings: an owner-drawn command that
// copies the game context (desktop_scene_dump.hpp), with a tooltip while
// the pointer rests on it.
class CopyButton final {
public:
  static constexpr UINT command = 0x7207;

  // Appends the icon to `bar`, the menu bar of `window`.
  CopyButton(HWND window, HMENU bar);
  ~CopyButton();
  CopyButton(const CopyButton &) = delete;
  CopyButton &operator=(const CopyButton &) = delete;

  // The icon's measuring and drawing, and the pointer over it. True when
  // the message is answered with `result`; false lets it go on.
  bool message(UINT message, WPARAM wparam, LPARAM lparam, LRESULT &result);
  void hide_tip();

private:
  void draw(const DRAWITEMSTRUCT &item) const;
  void hover(WPARAM hit, POINT at);
  [[nodiscard]] int scaled(int pixels) const;

  HWND window_{};
  HMENU bar_{};
  int position_{};
  HWND tip_{};
  bool tip_shown_{};
};

} // namespace phantomcrash
#endif
