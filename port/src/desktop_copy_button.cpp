#include "desktop_copy_button.hpp"

#ifdef _WIN32
#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>

namespace phantomcrash {
namespace {

constexpr int icon_width = 28;  // the item's width at 96 dpi
constexpr int sheet_size = 8;   // each of the icon's two sheets, at 96 dpi
constexpr int sheet_offset = 3; // how far the back sheet sits up and right
constexpr wchar_t tip_text[] = L"Copy game context";

COLORREF mixed(const COLORREF a, const COLORREF b, const int percent_b) {
  const auto mix = [&](const int x, const int y) {
    return (x * (100 - percent_b) + y * percent_b) / 100;
  };
  return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)),
             mix(GetBValue(a), GetBValue(b)));
}

} // namespace

CopyButton::CopyButton(const HWND window, const HMENU bar)
    : window_(window), bar_(bar), position_(GetMenuItemCount(bar)) {
  AppendMenuW(bar_, MF_OWNERDRAW, command, nullptr);
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
  InitCommonControlsEx(&controls);
  tip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                         WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT,
                         CW_USEDEFAULT, CW_USEDEFAULT, window_, nullptr, GetModuleHandleW(nullptr),
                         nullptr);
  if (tip_ != nullptr) {
    TTTOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_TRACK | TTF_ABSOLUTE;
    tool.hwnd = window_;
    tool.uId = command;
    tool.lpszText = const_cast<LPWSTR>(tip_text);
    SendMessageW(tip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
  }
}

CopyButton::~CopyButton() {
  if (tip_ != nullptr && IsWindow(tip_)) {
    DestroyWindow(tip_);
  }
}

int CopyButton::scaled(const int pixels) const {
  const UINT dpi = GetDpiForWindow(window_);
  return MulDiv(pixels, dpi != 0 ? static_cast<int>(dpi) : 96, 96);
}

bool CopyButton::message(const UINT message, const WPARAM wparam, const LPARAM lparam,
                         LRESULT &result) {
  switch (message) {
  case WM_MEASUREITEM: {
    auto &item = *reinterpret_cast<MEASUREITEMSTRUCT *>(lparam);
    if (item.CtlType != ODT_MENU || item.itemID != command) {
      return false;
    }
    item.itemWidth = static_cast<UINT>(scaled(icon_width));
    item.itemHeight = static_cast<UINT>(GetSystemMetrics(SM_CYMENU));
    result = TRUE;
    return true;
  }
  case WM_DRAWITEM: {
    const auto &item = *reinterpret_cast<const DRAWITEMSTRUCT *>(lparam);
    if (item.CtlType != ODT_MENU || item.itemID != command) {
      return false;
    }
    draw(item);
    result = TRUE;
    return true;
  }
  case WM_NCMOUSEMOVE:
    hover(wparam, POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
    return false;
  case WM_NCMOUSELEAVE:
  case WM_MOUSEMOVE:
  case WM_ENTERMENULOOP:
  case WM_KILLFOCUS:
    hide_tip();
    return false;
  default:
    return false;
  }
}

void CopyButton::draw(const DRAWITEMSTRUCT &item) const {
  const COLORREF bar = GetSysColor(COLOR_MENUBAR);
  const bool hot = (item.itemState & (ODS_HOTLIGHT | ODS_SELECTED)) != 0;
  const COLORREF back = hot ? mixed(bar, GetSysColor(COLOR_MENUHILIGHT), 25) : bar;
  HBRUSH fill = CreateSolidBrush(back);
  FillRect(item.hDC, &item.rcItem, fill);

  const int size = scaled(sheet_size);
  const int offset = scaled(sheet_offset);
  const int round = std::max(2, scaled(2));
  const int x = (item.rcItem.left + item.rcItem.right - size - offset) / 2;
  const int y = (item.rcItem.top + item.rcItem.bottom - size + offset) / 2;
  HPEN pen =
      CreatePen(PS_SOLID, std::max(1, scaled(1)),
                GetSysColor((item.itemState & ODS_GRAYED) ? COLOR_GRAYTEXT : COLOR_MENUTEXT));
  HGDIOBJ old_pen = SelectObject(item.hDC, pen);
  HGDIOBJ old_brush = SelectObject(item.hDC, fill);
  // The back sheet, then the front one over it, filled so it hides the overlap.
  RoundRect(item.hDC, x + offset, y - offset, x + offset + size, y - offset + size, round, round);
  RoundRect(item.hDC, x, y, x + size, y + size, round, round);
  SelectObject(item.hDC, old_brush);
  SelectObject(item.hDC, old_pen);
  DeleteObject(pen);
  DeleteObject(fill);
}

void CopyButton::hover(const WPARAM hit, const POINT at) {
  RECT rect{};
  const bool over = hit == HTMENU &&
                    GetMenuItemRect(window_, bar_, static_cast<UINT>(position_), &rect) &&
                    PtInRect(&rect, at);
  if (!over) {
    hide_tip();
    return;
  }
  TRACKMOUSEEVENT leave{sizeof(leave), TME_LEAVE | TME_NONCLIENT, window_, 0};
  TrackMouseEvent(&leave);
  if (tip_shown_ || tip_ == nullptr) {
    return;
  }
  TTTOOLINFOW tool{};
  tool.cbSize = sizeof(tool);
  tool.hwnd = window_;
  tool.uId = command;
  SendMessageW(tip_, TTM_TRACKPOSITION, 0, MAKELPARAM(rect.left, rect.bottom + scaled(4)));
  SendMessageW(tip_, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
  tip_shown_ = true;
}

void CopyButton::hide_tip() {
  if (!tip_shown_ || tip_ == nullptr) {
    return;
  }
  TTTOOLINFOW tool{};
  tool.cbSize = sizeof(tool);
  tool.hwnd = window_;
  tool.uId = command;
  SendMessageW(tip_, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
  tip_shown_ = false;
}

} // namespace phantomcrash
#endif
