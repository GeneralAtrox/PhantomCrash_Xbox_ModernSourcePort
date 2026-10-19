#pragma once
// Source of truth: Phangrade/integration. Keep the port copies identical.
#ifdef _WIN32
#include "phangrade_audio.hpp"
#include <commctrl.h>
#include <functional>
#include <string>
#include <utility>
#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#endif

namespace phangrade {
class Session {
  static constexpr UINT_PTR identity = 0x50484752;
  static constexpr UINT volume_first = 0x6d00, fullscreen_command = 0x6d20;
  std::wstring path_;
  HWND window_{};
  HWND input_window_{};
  HMENU menu_{}, bar_{};
  Audio audio_;
  int volume_ = 25;
  bool fullscreen_{}, restoring_{}, closing_{}, maximize_on_show_{};
  DWORD style_{};
  WINDOWPLACEMENT placement_{};
  std::function<bool()> get_fullscreen_;
  std::function<void(bool)> set_fullscreen_;
  std::wstring last_window_;

  int read(const wchar_t *section, const wchar_t *key, int fallback) const {
    return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, path_.c_str()));
  }
  void volume(int value) {
    volume_ = std::clamp(value, 0, 100);
    audio_.set(volume_);
    WritePrivateProfileStringW(L"audio", L"volume", std::to_wstring(volume_).c_str(),
                               path_.c_str());
  }
  bool fullscreen() const {
    return get_fullscreen_ ? get_fullscreen_() : fullscreen_;
  }
  void capture() {
    if (!window_ || restoring_ || closing_ || maximize_on_show_ || IsIconic(window_)) {
      return;
    }
    const bool full = fullscreen();
    if (!full && (GetWindowLongPtrW(window_, GWL_STYLE) & WS_CAPTION)) {
      GetWindowPlacement(window_, &placement_);
      placement_.showCmd = IsZoomed(window_) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    }
    const auto &r = placement_.rcNormalPosition;
    if (r.right <= r.left || r.bottom <= r.top) {
      return;
    }
    std::wstring data = std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L"," +
                        std::to_wstring(r.right) + L"," + std::to_wstring(r.bottom) + L"," +
                        std::to_wstring(placement_.showCmd) + L"," + std::to_wstring(full);
    if (data != last_window_ &&
        WritePrivateProfileStringW(L"window", L"placement", data.c_str(), path_.c_str())) {
      last_window_ = std::move(data);
    }
  }
  void toggle(bool enabled) {
    capture();
    if (set_fullscreen_) {
      restoring_ = true;
      if (!enabled) {
        SetMenu(window_, bar_);
      }
      set_fullscreen_(enabled);
      SetMenu(window_, fullscreen() ? nullptr : bar_);
      if (!enabled && !fullscreen()) {
        SetWindowPlacement(window_, &placement_);
      }
      DrawMenuBar(window_);
      restoring_ = false;
      capture();
      return;
    }
    if (enabled == fullscreen_) {
      return;
    }
    restoring_ = true;
    fullscreen_ = enabled;
    if (enabled) {
      style_ = static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE));
      MONITORINFO monitor{};
      monitor.cbSize = sizeof(monitor);
      GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
      SetMenu(window_, nullptr);
      SetWindowLongPtrW(window_, GWL_STYLE, style_ & ~WS_OVERLAPPEDWINDOW);
      const auto &r = monitor.rcMonitor;
      SetWindowPos(window_, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                   SWP_NOZORDER | SWP_FRAMECHANGED);
    } else {
      SetWindowLongPtrW(window_, GWL_STYLE, style_);
      SetMenu(window_, bar_);
      SetWindowPlacement(window_, &placement_);
      SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    restoring_ = false;
    capture();
  }
  void tick() {
    const int requested = std::clamp(read(L"audio", L"volume", volume_), 0, 100);
    if (requested != volume_) {
      audio_.set(volume_ = requested);
    } else {
      const int changed = audio_.changed_volume();
      if (changed >= 0 && changed != volume_) {
        volume(changed);
      }
    }
    if (menu_) {
      for (UINT i = 0; i <= 20; ++i) {
        CheckMenuItem(menu_, volume_first + i,
                      MF_BYCOMMAND |
                          (volume_ == static_cast<int>(i * 5) ? MF_CHECKED : MF_UNCHECKED));
      }
      CheckMenuItem(menu_, fullscreen_command,
                    MF_BYCOMMAND | (fullscreen() ? MF_CHECKED : MF_UNCHECKED));
    }
    capture();
  }
  static LRESULT CALLBACK message(HWND hwnd, UINT msg, WPARAM w, LPARAM l, UINT_PTR id,
                                  DWORD_PTR data) {
    auto &self = *reinterpret_cast<Session *>(data);
    if (msg == WM_TIMER && w == identity) {
      self.tick();
      return 0;
    }
    if (msg == WM_COMMAND && HIWORD(w) == 0 && l == 0) {
      const auto command = LOWORD(w);
      if (command >= volume_first && command <= volume_first + 20) {
        self.volume((command - volume_first) * 5);
        return 0;
      }
      if (command == fullscreen_command) {
        self.toggle(!self.fullscreen());
        return 0;
      }
    }
    if (msg == WM_SYSKEYDOWN && w == VK_RETURN && (l & (1LL << 29))) {
      if (!(l & (1LL << 30))) {
        self.toggle(!self.fullscreen());
      }
      return 0;
    }
    if (msg == WM_SYSCHAR && w == VK_RETURN) {
      return 0;
    }
    // Record the normal rectangle before SDL removes the window decorations.
    if (msg == WM_STYLECHANGING || msg == WM_EXITSIZEMOVE || msg == WM_CLOSE) {
      self.capture();
    }
    if (msg == WM_CLOSE) {
      self.closing_ = true;
    }
    if (msg == WM_WINDOWPOSCHANGED && self.maximize_on_show_ &&
        (reinterpret_cast<const WINDOWPOS *>(l)->flags & SWP_SHOWWINDOW)) {
      // Let the framework finish showing the window first: SDL restores a window it
      // did not maximize itself when it is shown, so maximize only afterwards.
      const auto result = DefSubclassProc(hwnd, msg, w, l);
      self.maximize_on_show_ = false;
      if (!self.fullscreen()) {
        ShowWindow(hwnd, SW_SHOWMAXIMIZED);
      }
      self.capture();
      return result;
    }
    if (msg == WM_DESTROY) {
      self.tick();
      KillTimer(hwnd, identity);
    }
    if (msg == WM_NCDESTROY) {
      RemoveWindowSubclass(hwnd, message, id);
      self.window_ = nullptr;
    }
    return DefSubclassProc(hwnd, msg, w, l);
  }

public:
  Session() {
    placement_.length = sizeof(placement_);
    wchar_t path[32768]{};
    const DWORD size = GetEnvironmentVariableW(L"PHANGRADE_SETTINGS", path, 32768);
    if (!size || size >= 32768) {
      return;
    }
    path_ = path;
    volume_ = std::clamp(read(L"audio", L"volume", 25), 0, 100);
    audio_.open(volume_);
  }
  bool active() const {
    return !path_.empty();
  }
  // Some Win32 ports focus a render child inside their main frame.
  void attach_input(void *input) {
    if (!window_ || !input || input == window_) {
      return;
    }
    input_window_ = static_cast<HWND>(input);
    SetWindowSubclass(input_window_, input_message, identity, reinterpret_cast<DWORD_PTR>(window_));
  }
  static LRESULT CALLBACK input_message(HWND hwnd, UINT msg, WPARAM w, LPARAM l, UINT_PTR id,
                                        DWORD_PTR frame) {
    if (msg == WM_SYSKEYDOWN && w == VK_RETURN && (l & (1LL << 29))) {
      return SendMessageW(reinterpret_cast<HWND>(frame), msg, w, l);
    }
    if (msg == WM_SYSCHAR && w == VK_RETURN) {
      return 0;
    }
    if (msg == WM_NCDESTROY) {
      RemoveWindowSubclass(hwnd, input_message, id);
    }
    return DefSubclassProc(hwnd, msg, w, l);
  }
  void attach(void *window, std::function<bool()> get = {}, std::function<void(bool)> set = {}) {
    if (!active() || !window) {
      return;
    }
    window_ = static_cast<HWND>(window);
    get_fullscreen_ = std::move(get);
    set_fullscreen_ = std::move(set);
    restoring_ = true;
    GetWindowPlacement(window_, &placement_);
    wchar_t stored[256]{};
    GetPrivateProfileStringW(L"window", L"placement", L"", stored, 256, path_.c_str());
    int left, top, right, bottom, show, full;
    bool restore_full = false;
    if (swscanf_s(stored, L"%d,%d,%d,%d,%d,%d", &left, &top, &right, &bottom, &show, &full) == 6 &&
        right > left && bottom > top && static_cast<long long>(right) - left <= 32768 &&
        static_cast<long long>(bottom) - top <= 32768) {
      placement_.rcNormalPosition = {left, top, right, bottom};
      placement_.showCmd = show == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
      placement_.flags = 0;
      // Windows adjusts placement to an available monitor if one was removed.
      // A window that is still hidden keeps its normal size until it is shown.
      maximize_on_show_ = placement_.showCmd == SW_SHOWMAXIMIZED && !IsWindowVisible(window_);
      if (maximize_on_show_) {
        placement_.showCmd = SW_HIDE;
      }
      SetWindowPlacement(window_, &placement_);
      if (maximize_on_show_) {
        placement_.showCmd = SW_SHOWMAXIMIZED;
      }
      restore_full = full != 0;
    }
    bar_ = GetMenu(window_);
    if (!bar_) {
      bar_ = CreateMenu();
      SetMenu(window_, bar_);
    }
    menu_ = CreatePopupMenu();
    HMENU levels = CreatePopupMenu();
    for (UINT i = 0; i <= 20; ++i) {
      const auto label = i == 0 ? std::wstring(L"Mute") : std::to_wstring(i * 5) + L"%";
      AppendMenuW(levels, MF_STRING, volume_first + i, label.c_str());
    }
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(levels), L"&Volume");
    AppendMenuW(menu_, MF_STRING, fullscreen_command, L"&Full screen\tAlt+Enter");
    AppendMenuW(bar_, MF_POPUP, reinterpret_cast<UINT_PTR>(menu_), L"Phangrade");
    DrawMenuBar(window_);
    SetWindowSubclass(window_, message, identity, reinterpret_cast<DWORD_PTR>(this));
    SetTimer(window_, identity, 250, nullptr);
    restoring_ = false;
    if (restore_full != fullscreen()) {
      toggle(restore_full);
    }
    tick();
  }
  ~Session() {
    if (input_window_) {
      RemoveWindowSubclass(input_window_, input_message, identity);
    }
    if (window_) {
      tick();
      KillTimer(window_, identity);
      RemoveWindowSubclass(window_, message, identity);
    }
  }
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;
};
} // namespace phangrade
#endif
