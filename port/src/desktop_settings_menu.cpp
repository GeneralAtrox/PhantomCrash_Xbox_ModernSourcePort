#include "desktop_settings_menu.hpp"
#include "desktop_audio_menu.hpp"
#include "desktop_cheats_menu.hpp"
#include "desktop_visuals_dialog.hpp"
#include "desktop_coop_dialog.hpp"

#include "desktop_frame_rate_dialog.hpp"
#include "desktop_controls_dialog.hpp"
#include "desktop_copy_button.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#endif

namespace phantomcrash {

#ifdef _WIN32
struct SettingsMenu::Impl {
  static constexpr UINT visuals_command = 0x7200;
  static constexpr UINT pause_unfocused_command = 0x7204;
  static constexpr UINT controls_command = 0x7205;
  static constexpr UINT coop_command = 0x7206;
  static constexpr UINT slai_command = 0x7209;
  static constexpr UINT skip_conversations_command = 0x7208;
  static constexpr UINT first_level_command = 0x7300;
  static constexpr UINT last_level_command = 0x73ff;

  Settings settings;
  bool interacted{};
  bool visible{};
  bool pointer_on_frame{}; // over the title or menu bar
  bool in_menu{};
  std::optional<int> level;
  std::optional<CheatRequest> cheat;
  bool copy_requested{};
  bool slai_toggle{};
  HWND window{};
  HMENU bar{}, menu{}, levels{}, audio{}, cheats{};
  std::unique_ptr<VisualsDialog> visuals;
  std::unique_ptr<CoopDialog> coop;
  std::unique_ptr<CopyButton> copy;

  void check(UINT command, bool on) const {
    CheckMenuItem(menu, command, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
  }
  void refresh() const {
    if (audio) {
      refresh_audio_menu(audio, settings.audio);
    }
    if (cheats) {
      refresh_cheats_menu(cheats, settings.cheats);
    }
    check(pause_unfocused_command, settings.pause_unfocused);
    check(slai_command, settings.slai_enabled);
    check(skip_conversations_command, settings.skip_conversations);
    if (coop) {
      coop->refresh();
    }
    if (visuals) {
      visuals->refresh();
    }
  }
  bool command(UINT id) {
    if (audio_menu_command(id, settings.audio)) {
      refresh();
      interacted = true;
      return true;
    }
    if (cheats_menu_command(id, settings.cheats, cheat)) {
      refresh();
      interacted = true;
      return true;
    }
    if (id >= first_level_command && id <= last_level_command) {
      level = static_cast<int>(id - first_level_command);
      interacted = true;
      return true;
    }
    switch (id) {
    case CopyButton::command:
      copy->hide_tip();
      copy_requested = true;
      break;
    case coop_command:
      coop->show();
      break;
    case slai_command:
      slai_toggle = true;
      break;
    case visuals_command:
      visuals->show();
      break;
    case pause_unfocused_command:
      settings.pause_unfocused = !settings.pause_unfocused;
      break;
    case skip_conversations_command:
      settings.skip_conversations = !settings.skip_conversations;
      break;
    case controls_command:
      edit_host_controls(window, settings.host_controls);
      break;
    default:
      return false;
    }
    refresh();
    interacted = true;
    return true;
  }
  // Shows or hides the bar without changing the size of the game's client area.
  void show_bar(bool show) {
    if (show == visible) {
      return;
    }
    visible = show;
    RECT client{};
    GetClientRect(window, &client);
    SetMenu(window, show ? bar : nullptr);
    if (IsZoomed(window) || (GetWindowLongPtrW(window, GWL_STYLE) & WS_POPUP)) {
      return;
    }
    AdjustWindowRectEx(&client, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), show,
                       static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)));
    SetWindowPos(window, nullptr, 0, 0, client.right - client.left, client.bottom - client.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }
  // Where the pointer is on the window frame, and whether a menu is open.
  static void track_frame(HWND hwnd, UINT message, Impl &self) {
    switch (message) {
    case WM_NCMOUSEMOVE:
      if (!self.pointer_on_frame) {
        TRACKMOUSEEVENT leave{sizeof(leave), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
        TrackMouseEvent(&leave);
        self.pointer_on_frame = true;
      }
      break;
    case WM_NCMOUSELEAVE:
    case WM_MOUSEMOVE:
      self.pointer_on_frame = false;
      break;
    case WM_ENTERMENULOOP:
      self.in_menu = true;
      break;
    case WM_EXITMENULOOP:
      self.in_menu = false;
      break;
    default:
      break;
    }
  }
  static LRESULT CALLBACK message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                  UINT_PTR id, DWORD_PTR data) {
    auto &self = *reinterpret_cast<Impl *>(data);
    // Alt and F10 control cursor release; do not enter the native keyboard
    // menu loop when either key is released. Mouse and mnemonic access remain.
    if (message == WM_SYSCOMMAND && (wparam & 0xfff0) == SC_KEYMENU && lparam == 0) {
      return 0;
    }
    // Windows uses VK_RETURN for both Enter keys; bit 24 distinguishes the
    // keypad key. Handle both here before native Alt-menu processing eats it.
    if (message == WM_SYSKEYDOWN && wparam == VK_RETURN && (lparam & (1LL << 29)) &&
        !(lparam & (1LL << 30))) {
      self.settings.fullscreen = !self.settings.fullscreen;
      self.refresh();
      self.interacted = true;
      return 0;
    }
    if (LRESULT result = 0; self.copy && self.copy->message(message, wparam, lparam, result)) {
      return result;
    }
    track_frame(hwnd, message, self);
    if (message == WM_EXITMENULOOP) {
      self.interacted = true;
    }
    if (message == WM_COMMAND && HIWORD(wparam) == 0 && lparam == 0 &&
        self.command(LOWORD(wparam))) {
      return 0;
    }
    if (message == WM_NCDESTROY) {
      RemoveWindowSubclass(hwnd, &Impl::message, id);
      self.window = nullptr;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
  }
  ~Impl() {
    visuals.reset();
    coop.reset();
    copy.reset();
    if (window) {
      RemoveWindowSubclass(window, &Impl::message, 1);
      SetMenu(window, nullptr);
    }
    if (bar) {
      DestroyMenu(bar);
    }
  }
};

SettingsMenu::SettingsMenu(void *native_window, const Settings &initial)
    : impl_(std::make_unique<Impl>()) {
  auto &state = *impl_;
  state.settings = initial;
  auto *hwnd = static_cast<HWND>(native_window);
  if (hwnd == nullptr) {
    return;
  }
  state.bar = CreateMenu();
  state.menu = CreatePopupMenu();
  if (!state.bar || !state.menu ||
      !AppendMenuW(state.bar, MF_POPUP, reinterpret_cast<UINT_PTR>(state.menu), L"&Settings")) {
    if (state.menu) {
      DestroyMenu(state.menu);
    }
    throw std::runtime_error("Unable to create the Settings menu");
  }
  state.visuals = std::make_unique<VisualsDialog>(hwnd, state.settings);
  state.coop = std::make_unique<CoopDialog>(hwnd, state.settings);
  if (!AppendMenuW(state.menu, MF_STRING, Impl::coop_command, L"&Co-op...") ||
      !AppendMenuW(state.menu, MF_STRING, Impl::slai_command,
                   L"S.L.A.I. content (restart required)") ||
      !AppendMenuW(state.menu, MF_STRING, Impl::visuals_command, L"&Visuals...") ||
      !AppendMenuW(state.menu, MF_STRING, Impl::controls_command,
                   L"&Keyboard / Controller Controls...") ||
      !AppendMenuW(state.menu, MF_STRING, Impl::pause_unfocused_command,
                   L"&Pause When Unfocused") ||
      !AppendMenuW(state.menu, MF_STRING, Impl::skip_conversations_command,
                   L"&Skip Story Conversations")) {
    throw std::runtime_error("Unable to fill the Settings menu");
  }
  state.audio = static_cast<HMENU>(create_audio_menu(state.menu, state.settings.audio));
  state.copy = std::make_unique<CopyButton>(hwnd, state.bar);
  HMENU tools = CreatePopupMenu();
  state.levels = CreatePopupMenu();
  if (!tools || !state.levels ||
      !AppendMenuW(tools, MF_POPUP, reinterpret_cast<UINT_PTR>(state.levels), L"&Level Select") ||
      !AppendMenuW(state.bar, MF_POPUP, reinterpret_cast<UINT_PTR>(tools), L"&Tools")) {
    throw std::runtime_error("Unable to create the Tools menu");
  }
  state.cheats = static_cast<HMENU>(create_cheats_menu(tools));
  state.refresh();
  if (!SetWindowSubclass(hwnd, &Impl::message, 1, reinterpret_cast<DWORD_PTR>(&state))) {
    throw std::runtime_error("Unable to connect the Settings menu");
  }
  state.window = hwnd;
}

SettingsMenu::~SettingsMenu() = default;

bool SettingsMenu::take_slai_toggle() {
  return std::exchange(impl_->slai_toggle, false);
}
void SettingsMenu::set_slai_enabled(bool enabled) {
  impl_->settings.slai_enabled = enabled;
  impl_->refresh();
}

const Settings &SettingsMenu::settings() const {
  return impl_->settings;
}

void SettingsMenu::set_fullscreen(const bool enabled) {
  impl_->settings.fullscreen = enabled;
  if (impl_->menu) {
    impl_->refresh();
  }
}

void SettingsMenu::set_visible(const bool visible) {
  if (impl_->window) {
    impl_->show_bar(visible);
  }
}

bool SettingsMenu::frame_active() const {
  return impl_->pointer_on_frame || impl_->in_menu;
}

bool SettingsMenu::take_interaction() {
  const bool preview = impl_->visuals && impl_->visuals->take_interaction();
  const bool coop = impl_->coop && impl_->coop->take_interaction();
  return std::exchange(impl_->interacted, false) || preview || coop;
}

bool SettingsMenu::visuals_open() const {
  return impl_->visuals && impl_->visuals->open();
}

void SettingsMenu::set_levels(const std::vector<std::string> &labels) {
  HMENU levels = impl_->levels;
  if (levels == nullptr) {
    return;
  }
  while (GetMenuItemCount(levels) > 0) {
    DeleteMenu(levels, 0, MF_BYPOSITION);
  }
  const auto count = std::min<std::size_t>(labels.size(), Impl::last_level_command -
                                                              Impl::first_level_command + 1);
  for (std::size_t index = 0; index < count; ++index) {
    const std::string &label = labels[index];
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, label.data(), static_cast<int>(label.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, label.data(), static_cast<int>(label.size()), wide.data(),
                        size);
    AppendMenuW(levels, MF_STRING, Impl::first_level_command + static_cast<UINT>(index),
                wide.c_str());
  }
  if (count == 0) {
    AppendMenuW(levels, MF_STRING | MF_GRAYED, 0, L"(none)");
  }
  if (impl_->window != nullptr) {
    DrawMenuBar(impl_->window);
  }
}

std::optional<int> SettingsMenu::take_level() {
  return std::exchange(impl_->level, std::nullopt);
}

bool SettingsMenu::take_copy() {
  return std::exchange(impl_->copy_requested, false);
}

void SettingsMenu::set_cheat_days(const std::vector<std::string> &labels) {
  if (impl_->cheats != nullptr) {
    phantomcrash::set_cheat_days(impl_->cheats, labels);
  }
}

std::optional<CheatRequest> SettingsMenu::take_cheat() {
  return std::exchange(impl_->cheat, std::nullopt);
}
#else
struct SettingsMenu::Impl {
  Settings settings;
};

SettingsMenu::SettingsMenu(void *, const Settings &initial) : impl_(std::make_unique<Impl>()) {
  impl_->settings = initial;
}
SettingsMenu::~SettingsMenu() = default;
const Settings &SettingsMenu::settings() const {
  return impl_->settings;
}
void SettingsMenu::set_fullscreen(const bool enabled) {
  impl_->settings.fullscreen = enabled;
}
void SettingsMenu::set_visible(bool) {}
bool SettingsMenu::take_slai_toggle() {
  return false;
}
void SettingsMenu::set_slai_enabled(bool enabled) {
  impl_->settings.slai_enabled = enabled;
}
bool SettingsMenu::visuals_open() const {
  return false;
}
bool SettingsMenu::frame_active() const {
  return false;
}
bool SettingsMenu::take_interaction() {
  return false;
}
void SettingsMenu::set_levels(const std::vector<std::string> &) {}
std::optional<int> SettingsMenu::take_level() {
  return std::nullopt;
}
bool SettingsMenu::take_copy() {
  return false;
}
void SettingsMenu::set_cheat_days(const std::vector<std::string> &) {}
std::optional<CheatRequest> SettingsMenu::take_cheat() {
  return std::nullopt;
}
#endif

} // namespace phantomcrash
