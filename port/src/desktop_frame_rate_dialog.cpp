#include "desktop_frame_rate_dialog.hpp"
#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <string>
#include <vector>

// Themed common controls (trackbar, buttons) for this dialog and the app.
#pragma comment(linker,                                                                            \
                "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' "     \
                "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' "   \
                "language='*'\"")

namespace phantomcrash {
namespace {
enum : int { id_limit = 101, id_slider, id_value, id_unit, id_hint, id_marks };

struct DialogState {
  FrameRateSettings settings;
  bool updating{};
};

std::vector<WORD> dialog_template() {
  std::vector<WORD> words(sizeof(DLGTEMPLATE) / sizeof(WORD));
  auto &dialog = *reinterpret_cast<DLGTEMPLATE *>(words.data());
  dialog.style = DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU;
  dialog.cx = 230;
  dialog.cy = 94;
  auto append = [&](const wchar_t *text) {
    for (; *text; ++text) {
      words.push_back(static_cast<WORD>(*text));
    }
    words.push_back(0);
  };
  words.push_back(0); // no menu
  words.push_back(0); // default dialog class
  append(L"Frame Rate Cap");
  words.push_back(9); // point size
  append(L"Segoe UI");
  return words;
}

HWND control(HWND dialog, int id) {
  return GetDlgItem(dialog, id);
}

HWND add(HWND dialog, const wchar_t *type, const wchar_t *text, DWORD style, int id, int x, int y,
         int cx, int cy) {
  RECT r{x, y, x + cx, y + cy};
  MapDialogRect(dialog, &r);
  HWND child =
      CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, r.left, r.top, r.right - r.left,
                      r.bottom - r.top, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                      GetModuleHandleW(nullptr), nullptr);
  SendMessageW(child, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), TRUE);
  return child;
}

void show_value(HWND dialog, DialogState &state) {
  state.updating = true;
  SetDlgItemInt(dialog, id_value, static_cast<UINT>(state.settings.cap), FALSE);
  state.updating = false;
}

// Repaints the arrow strip so the highlighted snap point follows the value.
void refresh_arrows(HWND dialog) {
  InvalidateRect(control(dialog, id_marks), nullptr, TRUE);
}

void enable_cap(HWND dialog, bool enabled) {
  for (int id : {id_slider, id_value, id_unit}) {
    EnableWindow(control(dialog, id), enabled);
  }
  refresh_arrows(dialog);
}

// Slider-client x of a value, from the two interior tick marks the trackbar
// placed itself (it maps positions linearly between them). The marks are not
// drawn; the arrow strip shows every snap point instead.
int value_x(HWND slider, int value) {
  const auto x60 = static_cast<int>(SendMessageW(slider, TBM_GETTICPOS, 0, 0));
  const auto x120 = static_cast<int>(SendMessageW(slider, TBM_GETTICPOS, 1, 0));
  return x60 + (value - 60) * (x120 - x60) / 60;
}

// Arrows under the track, one per snap point, in the owner-drawn strip.
void draw_snap_arrows(HWND dialog, const DRAWITEMSTRUCT &item, int current) {
  HWND slider = control(dialog, id_slider);
  FillRect(item.hDC, &item.rcItem, GetSysColorBrush(COLOR_BTNFACE));
  RECT size{0, 0, 3, 3};
  MapDialogRect(dialog, &size);
  const int half = std::max(3, static_cast<int>(size.right)), height = std::max(4, half + 1);
  const bool enabled = IsWindowEnabled(slider);
  for (int point : frame_cap_snap_points) {
    POINT at{value_x(slider, point), 0};
    MapWindowPoints(slider, item.hwndItem, &at, 1);
    const POINT arrow[3] = {{at.x, 0}, {at.x - half, height}, {at.x + half, height}};
    const int colour = !enabled           ? COLOR_GRAYTEXT
                       : point == current ? COLOR_HIGHLIGHT
                                          : COLOR_BTNTEXT;
    HGDIOBJ old_brush = SelectObject(item.hDC, GetSysColorBrush(colour));
    HGDIOBJ old_pen = SelectObject(item.hDC, GetStockObject(NULL_PEN));
    Polygon(item.hDC, arrow, 3);
    SelectObject(item.hDC, old_pen);
    SelectObject(item.hDC, old_brush);
  }
}

void initialize(HWND dialog, DialogState &state) {
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
  InitCommonControlsEx(&controls);
  add(dialog, L"BUTTON", L"&Limit frame rate", BS_AUTOCHECKBOX | WS_TABSTOP, id_limit, 10, 8, 150,
      12);
  HWND slider = add(dialog, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_BOTTOM | WS_TABSTOP, id_slider, 6,
                    24, 164, 16);
  add(dialog, L"STATIC", L"", SS_OWNERDRAW, id_marks, 6, 39, 164, 7);
  add(dialog, L"EDIT", L"", ES_NUMBER | ES_CENTER | WS_BORDER | WS_TABSTOP, id_value, 176, 28, 28,
      13);
  add(dialog, L"STATIC", L"FPS", SS_LEFT, id_unit, 207, 30, 20, 10);
  add(dialog, L"STATIC", L"Arrows mark the snap points. Type any value from 30 to 144.", SS_LEFT,
      id_hint, 10, 54, 212, 18);
  add(dialog, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 116, 72, 50, 14);
  add(dialog, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 172, 72, 50, 14);

  SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(frame_cap_minimum, frame_cap_maximum));
  SendMessageW(slider, TBM_SETLINESIZE, 0, frame_cap_step);
  SendMessageW(slider, TBM_SETPAGESIZE, 0, 10);
  SendMessageW(slider, TBM_SETTIC, 0, 60);
  SendMessageW(slider, TBM_SETTIC, 0, 120);
  SendMessageW(slider, TBM_SETPOS, TRUE, state.settings.cap);
  SendDlgItemMessageW(dialog, id_value, EM_SETLIMITTEXT, 3, 0);
  CheckDlgButton(dialog, id_limit, state.settings.cap_enabled ? BST_CHECKED : BST_UNCHECKED);
  enable_cap(dialog, state.settings.cap_enabled);
  show_value(dialog, state);
  SetFocus(control(dialog, id_limit));
}

// Accepts the typed value if valid; otherwise beeps and keeps the dialog open.
bool commit_typed(HWND dialog, DialogState &state) {
  wchar_t text[8]{};
  GetDlgItemTextW(dialog, id_value, text, 8);
  const auto value = parse_frame_cap(text);
  if (!value) {
    MessageBeep(MB_ICONWARNING);
    SetFocus(control(dialog, id_value));
    return false;
  }
  state.settings.cap = *value;
  show_value(dialog, state);
  SendDlgItemMessageW(dialog, id_slider, TBM_SETPOS, TRUE, *value);
  refresh_arrows(dialog);
  return true;
}

INT_PTR CALLBACK procedure(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  auto *state = reinterpret_cast<DialogState *>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
  case WM_INITDIALOG:
    SetWindowLongPtrW(dialog, DWLP_USER, lparam);
    initialize(dialog, *reinterpret_cast<DialogState *>(lparam));
    return FALSE;
  case WM_HSCROLL:
    if (state && reinterpret_cast<HWND>(lparam) == control(dialog, id_slider)) {
      HWND slider = control(dialog, id_slider);
      const int position = static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0));
      const int snapped = snap_frame_cap(position);
      if (snapped != position) {
        SendMessageW(slider, TBM_SETPOS, TRUE, snapped);
      }
      state->settings.cap = snapped;
      show_value(dialog, *state);
      refresh_arrows(dialog);
    }
    return TRUE;
  case WM_DRAWITEM:
    if (state && wparam == id_marks) {
      draw_snap_arrows(dialog, *reinterpret_cast<const DRAWITEMSTRUCT *>(lparam),
                       state->settings.cap);
      return TRUE;
    }
    return FALSE;
  case WM_NOTIFY: {
    auto *header = reinterpret_cast<NMHDR *>(lparam);
    if (state && header->idFrom == id_slider && header->code == NM_CUSTOMDRAW) {
      auto *draw = reinterpret_cast<NMCUSTOMDRAW *>(lparam);
      LRESULT result = CDRF_DODEFAULT;
      if (draw->dwDrawStage == CDDS_PREPAINT) {
        result = CDRF_NOTIFYITEMDRAW;
      } else if (draw->dwDrawStage == CDDS_ITEMPREPAINT && draw->dwItemSpec == TBCD_TICS) {
        result = CDRF_SKIPDEFAULT; // the arrow strip replaces the tick marks
      }
      SetWindowLongPtrW(dialog, DWLP_MSGRESULT, result);
      return TRUE;
    }
    return FALSE;
  }
  case WM_COMMAND:
    if (!state) {
      return FALSE;
    }
    switch (LOWORD(wparam)) {
    case id_limit:
      state->settings.cap_enabled = IsDlgButtonChecked(dialog, id_limit) == BST_CHECKED;
      enable_cap(dialog, state->settings.cap_enabled);
      refresh_arrows(dialog);
      return TRUE;
    case id_value:
      if (HIWORD(wparam) == EN_CHANGE && !state->updating) {
        wchar_t text[8]{};
        GetDlgItemTextW(dialog, id_value, text, 8);
        const auto typed = parse_frame_cap(text);
        const int raw = _wtoi(text);
        // Follow complete in-range values live; clamp only when committing.
        if (typed && *typed == raw) {
          state->settings.cap = *typed;
          SendDlgItemMessageW(dialog, id_slider, TBM_SETPOS, TRUE, *typed);
          refresh_arrows(dialog);
        }
      } else if (HIWORD(wparam) == EN_KILLFOCUS && !state->updating) {
        wchar_t text[8]{};
        GetDlgItemTextW(dialog, id_value, text, 8);
        if (parse_frame_cap(text)) {
          commit_typed(dialog, *state);
        }
      }
      return TRUE;
    case IDOK:
      if (!state->settings.cap_enabled || commit_typed(dialog, *state)) {
        EndDialog(dialog, IDOK);
      }
      return TRUE;
    case IDCANCEL:
      EndDialog(dialog, IDCANCEL);
      return TRUE;
    }
    return FALSE;
  }
  return FALSE;
}
} // namespace

bool edit_frame_rate_cap(void *owner, FrameRateSettings &settings) {
  DialogState state{settings};
  const auto words = dialog_template();
  const INT_PTR result = DialogBoxIndirectParamW(
      GetModuleHandleW(nullptr), reinterpret_cast<const DLGTEMPLATE *>(words.data()),
      static_cast<HWND>(owner), &procedure, reinterpret_cast<LPARAM>(&state));
  if (result != IDOK) {
    return false;
  }
  settings = state.settings;
  return true;
}
} // namespace phantomcrash
#else
namespace phantomcrash {
bool edit_frame_rate_cap(void *, FrameRateSettings &) {
  return false;
}
} // namespace phantomcrash
#endif
