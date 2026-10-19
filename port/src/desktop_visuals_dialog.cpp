#include "desktop_visuals_dialog.hpp"
#include "desktop_frame_rate_dialog.hpp"
#include <array>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#endif

namespace phantomcrash {
#ifdef _WIN32
struct VisualsDialog::Impl {
  HWND owner{}, window{};
  Settings &settings;
  bool changed{};
  enum {
    fullscreen = 100,
    vsync,
    counter,
    dynamic,
    volumetric,
    smaa,
    blur,
    distortion,
    tint,
    soft,
    sharp,
    cap
  };
  struct Toggle {
    int id;
    const wchar_t *label;
    bool *value;
  };
  auto toggles() {
    return std::array<Toggle, 11>{
        {{fullscreen, L"Fullscreen (F11 / Alt + Enter)", &settings.fullscreen},
         {vsync, L"VSync", &settings.frame_rate.vsync},
         {counter, L"Frame rate counter", &settings.frame_counter},
         {dynamic, L"Dynamic lighting and shadows", &settings.post_process.dynamic_lighting},
         {volumetric, L"Light beams in the air (with dynamic lighting)",
          &settings.post_process.volumetric_light},
         {smaa, L"SMAA anti-aliasing", &settings.post_process.smaa},
         {blur, L"Depth blur", &settings.post_process.depth_blur},
         {distortion, L"Shinjuku distortion", &settings.post_process.distortion},
         {tint, L"Stage colour tint and levels", &settings.post_process.colour_tint},
         {soft, L"Original softness (640x480 look)", &settings.post_process.original_softness},
         {sharp, L"Sharp text (MS Gothic)", &settings.post_process.sharp_text}}};
  }
  void refresh() {
    if (!window) {
      return;
    }
    for (auto t : toggles()) {
      CheckDlgButton(window, t.id, *t.value ? BST_CHECKED : BST_UNCHECKED);
    }
    std::wstring text =
        settings.frame_rate.cap_enabled
            ? L"Frame rate cap: " + std::to_wstring(settings.frame_rate.cap) + L" FPS..."
            : L"Frame rate cap: Off...";
    SetDlgItemTextW(window, cap, text.c_str());
  }
  void control(const wchar_t *kind, const wchar_t *label, DWORD style, int id, int y, int height) {
    HWND child = CreateWindowExW(0, kind, label, WS_CHILD | WS_VISIBLE | style, 18, y, 340, height,
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
      int y = 16;
      self->control(L"STATIC", L"Changes apply immediately and are saved.", 0, 0, y, 25);
      y += 34;
      for (auto t : self->toggles()) {
        self->control(L"BUTTON", t.label, BS_AUTOCHECKBOX | WS_TABSTOP, t.id, y, 27);
        y += 32;
      }
      self->control(L"BUTTON", L"Frame rate cap...", BS_PUSHBUTTON | WS_TABSTOP, cap, y + 8, 30);
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
      if (LOWORD(wp) == cap) {
        edit_frame_rate_cap(hwnd, self->settings.frame_rate);
        self->changed = true;
        self->refresh();
        return 0;
      }
    }
    if (msg == WM_SYSKEYDOWN && wp == VK_RETURN && (lp & (1LL << 29)) && !(lp & (1LL << 30))) {
      self->settings.fullscreen = !self->settings.fullscreen;
      self->changed = true;
      self->refresh();
      return 0;
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
VisualsDialog::VisualsDialog(void *owner, Settings &settings)
    : impl_(std::make_unique<Impl>(static_cast<HWND>(owner), nullptr, settings)) {}
VisualsDialog::~VisualsDialog() = default;
void VisualsDialog::show() {
  auto &s = *impl_;
  if (s.window) {
    SetForegroundWindow(s.window);
    return;
  }
  WNDCLASSW cls{};
  cls.lpfnWndProc = Impl::message;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpszClassName = L"PhantomCrashVisuals";
  cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  RegisterClassW(&cls);
  RECT rect{0, 0, 378, 460};
  constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
  AdjustWindowRectEx(&rect, style, FALSE, WS_EX_TOOLWINDOW);
  CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, L"Visuals", style | WS_VISIBLE,
                  CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                  s.owner, nullptr, cls.hInstance, &s);
}
void VisualsDialog::refresh() {
  impl_->refresh();
}
bool VisualsDialog::open() const {
  return impl_->window != nullptr;
}
bool VisualsDialog::take_interaction() {
  return std::exchange(impl_->changed, false);
}
#else
struct VisualsDialog::Impl {};
VisualsDialog::VisualsDialog(void *, Settings &) : impl_(std::make_unique<Impl>()) {}
VisualsDialog::~VisualsDialog() = default;
void VisualsDialog::show() {}
void VisualsDialog::refresh() {}
bool VisualsDialog::open() const {
  return false;
}
bool VisualsDialog::take_interaction() {
  return false;
}
#endif
} // namespace phantomcrash
