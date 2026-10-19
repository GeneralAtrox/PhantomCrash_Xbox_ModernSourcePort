#include "desktop_coop_dialog.hpp"
#include <array>
#include <SDL3/SDL_log.h>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#endif

namespace phantomcrash {
#ifdef _WIN32
struct CoopDialog::Impl {
  HWND owner{}, window{};
  Settings &settings;
  bool changed{};
  enum { enabled = 100, shootdowns, cash, bosses, offset, multiplier, apply };
  struct Toggle {
    int id;
    const wchar_t *label;
    bool *value;
  };
  auto toggles() {
    return std::array<Toggle, 4>{
        {{enabled, L"Enable experimental Co-op", &settings.experimental_coop},
         {shootdowns, L"Share shootdown credit", &settings.coop.share_shootdowns},
         {cash, L"Share cash (full payout for each player)", &settings.coop.share_cash},
         {bosses, L"Share boss defeats and contest clears", &settings.coop.share_boss_clears}}};
  }
  void refresh() {
    if (!window) {
      return;
    }
    for (auto t : toggles()) {
      CheckDlgButton(window, t.id, *t.value ? BST_CHECKED : BST_UNCHECKED);
    }
    // Do not overwrite edits when another host preference refreshes this dialog.
  }
  void load_values() {
    SetDlgItemInt(window, offset, settings.coop.enemy_level_offset, TRUE);
    SetDlgItemInt(window, multiplier, settings.coop.kill_ny_percent, FALSE);
  }
  bool save_values() {
    BOOL valid_offset{}, valid_multiplier{};
    const int level = static_cast<int>(GetDlgItemInt(window, offset, &valid_offset, TRUE));
    const unsigned percent = GetDlgItemInt(window, multiplier, &valid_multiplier, FALSE);
    if (!valid_offset || level < -99 || level > 99 || !valid_multiplier || percent > 1000) {
      MessageBoxW(window, L"Enemy levels: -99 to +99. Kill NY: 0 to 1000 percent.",
                  L"Check Co-op settings", MB_OK | MB_ICONINFORMATION);
      return false;
    }
    settings.coop.enemy_level_offset = level;
    settings.coop.kill_ny_percent = static_cast<int>(percent);
    changed = true;
    SDL_Log("Co-op settings applied: enemy level offset=%d kill NY=%d%%", level, percent);
    return true;
  }
  void control(const wchar_t *kind, const wchar_t *label, DWORD style, int id, int y, int height) {
    HWND child = CreateWindowExW(0, kind, label, WS_CHILD | WS_VISIBLE | style, 18, y, 410, height,
                                 window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 GetModuleHandleW(nullptr), nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                 TRUE);
  }
  static LRESULT CALLBACK message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto *self = reinterpret_cast<Impl *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
      self = static_cast<Impl *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
      self->window = hwnd;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) {
      return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (msg == WM_CREATE) {
      self->control(
          L"STATIC",
          L"Changes apply to new matches.\nSharing includes only players in the same match.", 0, 0,
          12, 38);
      int y = 60;
      for (auto t : self->toggles()) {
        self->control(L"BUTTON", t.label, BS_AUTOCHECKBOX | WS_TABSTOP, t.id, y, 27);
        y += 32;
      }
      self->control(L"STATIC", L"Enemy and boss level offset (-99 to +99):", 0, 0, 195, 22);
      self->control(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, offset, 218, 25);
      self->control(L"STATIC", L"Robot kill NY (%): 100 = 1x, 200 = 2x; maximum 1000", 0, 0, 253,
                    22);
      self->control(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_NUMBER, multiplier, 276, 25);
      self->control(L"BUTTON", L"Apply level and NY values", BS_PUSHBUTTON | WS_TABSTOP, apply, 316,
                    30);
      SendDlgItemMessageW(hwnd, offset, EM_SETLIMITTEXT, 4, 0);
      SendDlgItemMessageW(hwnd, multiplier, EM_SETLIMITTEXT, 4, 0);
      self->load_values();
      self->refresh();
      return 0;
    }
    if (msg == WM_COMMAND && HIWORD(wp) == BN_CLICKED) {
      for (auto t : self->toggles()) {
        if (LOWORD(wp) == t.id) {
          *t.value = IsDlgButtonChecked(hwnd, t.id) == BST_CHECKED;
          self->changed = true;
          self->refresh();
          return 0;
        }
      }
      if (LOWORD(wp) == apply) {
        self->save_values();
        return 0;
      }
    }
    if (msg == WM_CLOSE) {
      DestroyWindow(hwnd);
      return 0;
    }
    if (msg == WM_NCDESTROY) {
      self->window = nullptr;
      self->changed = true;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
  ~Impl() {
    if (window) {
      DestroyWindow(window);
    }
  }
};
CoopDialog::CoopDialog(void *owner, Settings &settings)
    : impl_(std::make_unique<Impl>(static_cast<HWND>(owner), nullptr, settings)) {}
CoopDialog::~CoopDialog() = default;
void CoopDialog::show() {
  auto &s = *impl_;
  if (s.window) {
    SetForegroundWindow(s.window);
    return;
  }
  WNDCLASSW cls{};
  cls.lpfnWndProc = Impl::message;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpszClassName = L"PhantomCrashCoop";
  cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  RegisterClassW(&cls);
  RECT rect{0, 0, 448, 364};
  constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
  AdjustWindowRectEx(&rect, style, FALSE, WS_EX_TOOLWINDOW);
  CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, L"Co-op settings", style | WS_VISIBLE,
                  CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                  s.owner, nullptr, cls.hInstance, &s);
}
void CoopDialog::refresh() {
  impl_->refresh();
}
bool CoopDialog::open() const {
  return impl_->window != nullptr;
}
bool CoopDialog::take_interaction() {
  return std::exchange(impl_->changed, false);
}
#else
struct CoopDialog::Impl {};
CoopDialog::CoopDialog(void *, Settings &) : impl_(std::make_unique<Impl>()) {}
CoopDialog::~CoopDialog() = default;
void CoopDialog::show() {}
void CoopDialog::refresh() {}
bool CoopDialog::open() const {
  return false;
}
bool CoopDialog::take_interaction() {
  return false;
}
#endif
} // namespace phantomcrash
