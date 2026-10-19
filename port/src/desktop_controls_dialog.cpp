#include "desktop_controls_dialog.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>

namespace phantomcrash {
namespace {
enum : int {
  actions = 101,
  keys,
  pads,
  bind_key,
  bind_pad,
  clear_key,
  clear_pad,
  defaults,
  mouse_on,
  invert_y,
  sensitivity
};

struct DialogState {
  HostControls controls;
  std::vector<std::string> key_names;
  std::vector<std::string> pad_names;
};

std::wstring wide(const std::string &text) {
  const int size =
      MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
  return out;
}

std::string pad_label(const std::string &name) {
  static const std::pair<const char *, const char *> labels[] = {
      {"a", "A / South"},
      {"b", "B / East"},
      {"x", "X / West"},
      {"y", "Y / North"},
      {"leftx-", "Left stick left"},
      {"leftx+", "Left stick right"},
      {"lefty-", "Left stick up"},
      {"lefty+", "Left stick down"},
      {"rightx-", "Right stick left"},
      {"rightx+", "Right stick right"},
      {"righty-", "Right stick up"},
      {"righty+", "Right stick down"},
      {"lefttrigger+", "Left trigger"},
      {"righttrigger+", "Right trigger"},
      {"leftshoulder", "Left shoulder"},
      {"rightshoulder", "Right shoulder"},
      {"leftstick", "Left stick click"},
      {"rightstick", "Right stick click"},
      {"dpup", "D-pad up"},
      {"dpdown", "D-pad down"},
      {"dpleft", "D-pad left"},
      {"dpright", "D-pad right"},
      {"start", "Start"},
      {"back", "Back / Select"}};
  for (const auto &[key, label] : labels) {
    if (name == key) {
      return label;
    }
  }
  return name.empty() ? "Unassigned" : name;
}

std::vector<WORD> dialog_template() {
  std::vector<WORD> words((sizeof(DLGTEMPLATE) + 1) / sizeof(WORD));
  auto &dialog = *reinterpret_cast<DLGTEMPLATE *>(words.data());
  dialog.style = DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU;
  dialog.cx = 490;
  dialog.cy = 310;
  const auto append = [&](const wchar_t *text) {
    for (; *text; ++text) {
      words.push_back(static_cast<WORD>(*text));
    }
    words.push_back(0);
  };
  words.push_back(0);
  words.push_back(0);
  append(L"Keyboard, Controller and Mouse");
  words.push_back(9);
  append(L"Segoe UI");
  return words;
}

HWND add(HWND dialog, const wchar_t *type, const wchar_t *text, DWORD style, int id, int x, int y,
         int width, int height) {
  RECT r{x, y, x + width, y + height};
  MapDialogRect(dialog, &r);
  HWND child =
      CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, r.left, r.top, r.right - r.left,
                      r.bottom - r.top, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                      GetModuleHandleW(nullptr), nullptr);
  SendMessageW(child, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), TRUE);
  return child;
}

int selected(HWND dialog) {
  return static_cast<int>(SendDlgItemMessageW(dialog, actions, LB_GETCURSEL, 0, 0));
}

void select_binding(HWND dialog, const DialogState &state) {
  const int row = selected(dialog);
  if (row < 0 || row >= static_cast<int>(state.controls.bindings.size())) {
    return;
  }
  const HostBinding &binding = state.controls.bindings[static_cast<std::size_t>(row)];
  const auto select = [&](const int id, const std::vector<std::string> &names,
                          const std::string &name) {
    const auto found = std::ranges::find(names, name);
    SendDlgItemMessageW(dialog, id, CB_SETCURSEL,
                        found == names.end() ? 0 : static_cast<WPARAM>(found - names.begin()), 0);
  };
  select(keys, state.key_names, binding.keys.empty() ? "" : binding.keys.front());
  select(pads, state.pad_names, binding.pad);
}

void refresh(HWND dialog, const DialogState &state) {
  const int row = std::max(selected(dialog), 0);
  SendDlgItemMessageW(dialog, actions, LB_RESETCONTENT, 0, 0);
  for (const HostBinding &binding : state.controls.bindings) {
    std::string text = binding.label + "   [";
    for (const auto &key : binding.keys) {
      if (text.back() != '[') {
        text += " / ";
      }
      text += key;
    }
    text += "]   [" + pad_label(binding.pad) + "]";
    SendDlgItemMessageW(dialog, actions, LB_ADDSTRING, 0,
                        reinterpret_cast<LPARAM>(wide(text).c_str()));
  }
  SendDlgItemMessageW(dialog, actions, LB_SETCURSEL, static_cast<WPARAM>(row), 0);
  select_binding(dialog, state);
}

void show_mouse(HWND dialog, const HostControls &controls) {
  CheckDlgButton(dialog, mouse_on, controls.mouse_aim ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(dialog, invert_y, controls.invert_mouse_y ? BST_CHECKED : BST_UNCHECKED);
  wchar_t text[32];
  swprintf_s(text, L"%.2f", controls.mouse_degrees_per_pixel);
  SetDlgItemTextW(dialog, sensitivity, text);
}

void populate(HWND dialog, int id, const std::vector<std::string> &names) {
  for (const auto &name : names) {
    const auto label = wide(id == pads ? pad_label(name) : name.empty() ? "Unassigned" : name);
    SendDlgItemMessageW(dialog, id, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
  }
}

void create_controls(HWND dialog, DialogState &state) {
  add(dialog, L"STATIC",
      L"Select an action, choose an input, then Assign. F10 releases the mouse in battle.", 0, -1,
      10, 8, 475, 14);
  add(dialog, L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP | LBS_NOTIFY,
      actions, 10, 28, 285, 245);
  SendDlgItemMessageW(dialog, actions, LB_SETHORIZONTALEXTENT, 900, 0);
  add(dialog, L"STATIC", L"Keyboard", 0, -1, 305, 28, 170, 12);
  add(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, keys, 305, 44, 172,
      180);
  add(dialog, L"BUTTON", L"Assign key", BS_PUSHBUTTON | WS_TABSTOP, bind_key, 305, 68, 82, 20);
  add(dialog, L"BUTTON", L"Clear key", BS_PUSHBUTTON | WS_TABSTOP, clear_key, 395, 68, 82, 20);
  add(dialog, L"STATIC", L"Controller button / stick / trigger", 0, -1, 305, 102, 175, 12);
  add(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, pads, 305, 118, 172,
      160);
  add(dialog, L"BUTTON", L"Assign pad", BS_PUSHBUTTON | WS_TABSTOP, bind_pad, 305, 142, 82, 20);
  add(dialog, L"BUTTON", L"Clear pad", BS_PUSHBUTTON | WS_TABSTOP, clear_pad, 395, 142, 82, 20);
  add(dialog, L"BUTTON", L"Mouse aim and arm weapon fire", BS_AUTOCHECKBOX | WS_TABSTOP, mouse_on,
      305, 183, 180, 16);
  add(dialog, L"STATIC", L"Sensitivity (0.01 - 2.00)", 0, -1, 305, 207, 124, 14);
  add(dialog, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, sensitivity, 433, 204, 44, 20);
  add(dialog, L"BUTTON", L"Invert mouse Y", BS_AUTOCHECKBOX | WS_TABSTOP, invert_y, 305, 232, 160,
      16);
  add(dialog, L"STATIC", L"Left / right click: left / right arm", 0, -1, 305, 258, 175, 14);
  add(dialog, L"BUTTON", L"Restore defaults", BS_PUSHBUTTON | WS_TABSTOP, defaults, 10, 282, 95,
      20);
  add(dialog, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 343, 282, 62, 20);
  add(dialog, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 415, 282, 62, 20);
  state.key_names.emplace_back();
  for (int i = 1; i < SDL_SCANCODE_COUNT; ++i) {
    if (i == SDL_SCANCODE_F10 || i == SDL_SCANCODE_F11) {
      continue;
    }
    const char *name = SDL_GetScancodeName(static_cast<SDL_Scancode>(i));
    if (name && *name) {
      state.key_names.emplace_back(name);
    }
  }
  std::sort(state.key_names.begin() + 1, state.key_names.end());
  state.pad_names.emplace_back();
  for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
    if (const char *name = SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(i))) {
      state.pad_names.emplace_back(name);
    }
  }
  for (const char *axis : {"leftx", "lefty", "rightx", "righty"}) {
    state.pad_names.push_back(std::string(axis) + "-");
    state.pad_names.push_back(std::string(axis) + "+");
  }
  state.pad_names.emplace_back("lefttrigger+");
  state.pad_names.emplace_back("righttrigger+");
  populate(dialog, keys, state.key_names);
  populate(dialog, pads, state.pad_names);
  refresh(dialog, state);
  show_mouse(dialog, state.controls);
}

INT_PTR CALLBACK procedure(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  auto *state = reinterpret_cast<DialogState *>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (message == WM_INITDIALOG) {
    state = reinterpret_cast<DialogState *>(lparam);
    SetWindowLongPtrW(dialog, DWLP_USER, lparam);
    create_controls(dialog, *state);
    return TRUE;
  }
  if (message != WM_COMMAND || !state) {
    return FALSE;
  }
  const int command = LOWORD(wparam);
  if (command == IDCANCEL) {
    EndDialog(dialog, IDCANCEL);
    return TRUE;
  }
  if (command == IDOK) {
    wchar_t text[32]{};
    GetDlgItemTextW(dialog, sensitivity, text, 32);
    wchar_t *end{};
    const double value = wcstod(text, &end);
    if (end == text || *end || !std::isfinite(value) || value < 0.01 || value > 2.0) {
      MessageBoxW(dialog, L"Enter a mouse sensitivity from 0.01 to 2.00.", L"Controls", MB_OK);
      return TRUE;
    }
    state->controls.mouse_degrees_per_pixel = static_cast<float>(value);
    state->controls.mouse_aim = IsDlgButtonChecked(dialog, mouse_on) == BST_CHECKED;
    state->controls.invert_mouse_y = IsDlgButtonChecked(dialog, invert_y) == BST_CHECKED;
    EndDialog(dialog, IDOK);
    return TRUE;
  }
  if (command == defaults) {
    state->controls = HostControls{};
    refresh(dialog, *state);
    show_mouse(dialog, state->controls);
    return TRUE;
  }
  if (command == actions && HIWORD(wparam) == LBN_SELCHANGE) {
    select_binding(dialog, *state);
    return TRUE;
  }
  const int row = selected(dialog);
  if (row < 0 || row >= static_cast<int>(state->controls.bindings.size())) {
    return FALSE;
  }
  const std::string action = state->controls.bindings[static_cast<std::size_t>(row)].action;
  if (command == bind_key || command == clear_key) {
    const auto choice = SendDlgItemMessageW(dialog, keys, CB_GETCURSEL, 0, 0);
    if (choice != CB_ERR) {
      bind_host_key(state->controls, action,
                    command == clear_key ? ""
                                         : state->key_names.at(static_cast<std::size_t>(choice)));
    }
  } else if (command == bind_pad || command == clear_pad) {
    const auto choice = SendDlgItemMessageW(dialog, pads, CB_GETCURSEL, 0, 0);
    if (choice != CB_ERR) {
      bind_host_pad(state->controls, action,
                    command == clear_pad ? ""
                                         : state->pad_names.at(static_cast<std::size_t>(choice)));
    }
  } else {
    return FALSE;
  }
  refresh(dialog, *state);
  return TRUE;
}
} // namespace

bool edit_host_controls(void *owner, HostControls &controls) {
  DialogState state{controls};
  const auto words = dialog_template();
  const auto result = DialogBoxIndirectParamW(
      GetModuleHandleW(nullptr), reinterpret_cast<const DLGTEMPLATE *>(words.data()),
      static_cast<HWND>(owner), procedure, reinterpret_cast<LPARAM>(&state));
  if (result != IDOK) {
    return false;
  }
  controls = std::move(state.controls);
  return true;
}
} // namespace phantomcrash
#else
namespace phantomcrash {
bool edit_host_controls(void *, HostControls &) {
  return false;
}
} // namespace phantomcrash
#endif
