#include "desktop_cheats_menu.hpp"

#ifdef _WIN32
#include <windows.h>

#include <algorithm>
#include <array>
#include <stdexcept>

namespace phantomcrash {
namespace {
constexpr unsigned int first_switch = 0x7500;
constexpr unsigned int fill_hangars_command = 0x7510;
constexpr unsigned int all_items_command = 0x7511;
constexpr unsigned int first_day_command = 0x7600;
constexpr unsigned int last_day_command = 0x76ff;
// Go to Day's place in the menu.
constexpr unsigned int days_position = 7;

constexpr std::array<const wchar_t *, 4> switch_labels{
    L"Infinite &Money", L"&Health Never Reaches 0", L"&Ammo Never Reaches 0",
    L"&Unlock All Contests (First Ranker Included)"};

std::array<bool *, 4> switches(CheatOptions &options) {
  return {&options.infinite_money, &options.never_die, &options.infinite_ammo,
          &options.open_contests};
}

std::wstring wide(const std::string &text) {
  const int size =
      MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
  return result;
}
} // namespace

void *create_cheats_menu(void *parent) {
  HMENU popup = CreatePopupMenu();
  HMENU days = CreatePopupMenu();
  if (!popup || !days ||
      !AppendMenuW(static_cast<HMENU>(parent), MF_POPUP, reinterpret_cast<UINT_PTR>(popup),
                   L"&Cheats")) {
    if (popup) {
      DestroyMenu(popup);
    }
    if (days) {
      DestroyMenu(days);
    }
    throw std::runtime_error("Unable to create the Cheats menu");
  }
  for (unsigned int index = 0; index < switch_labels.size(); ++index) {
    AppendMenuW(popup, MF_STRING, first_switch + index, switch_labels[index]);
  }
  AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(popup, MF_STRING, fill_hangars_command, L"&Fill Hangars (One of Each Robot Type)");
  AppendMenuW(popup, MF_STRING, all_items_command, L"Add All &Items, Chips and Music");
  AppendMenuW(popup, MF_POPUP, reinterpret_cast<UINT_PTR>(days), L"Go to &Day");
  AppendMenuW(popup, MF_STRING | MF_GRAYED, 0, L"One-off changes apply as the next screen opens");
  AppendMenuW(days, MF_STRING | MF_GRAYED, 0, L"(no game data)");
  return popup;
}

void refresh_cheats_menu(void *menu, const CheatOptions &options) {
  CheatOptions copy = options;
  const auto values = switches(copy);
  for (unsigned int index = 0; index < values.size(); ++index) {
    CheckMenuItem(static_cast<HMENU>(menu), first_switch + index,
                  MF_BYCOMMAND | (*values[index] ? MF_CHECKED : MF_UNCHECKED));
  }
}

void set_cheat_days(void *menu, const std::vector<std::string> &labels) {
  HMENU days = GetSubMenu(static_cast<HMENU>(menu), days_position);
  if (days == nullptr) {
    return;
  }
  while (GetMenuItemCount(days) > 0) {
    DeleteMenu(days, 0, MF_BYPOSITION);
  }
  const auto count = std::min<std::size_t>(labels.size(), last_day_command - first_day_command + 1);
  for (std::size_t day = 0; day < count; ++day) {
    AppendMenuW(days, MF_STRING, first_day_command + static_cast<UINT>(day),
                wide(labels[day]).c_str());
  }
  if (count == 0) {
    AppendMenuW(days, MF_STRING | MF_GRAYED, 0, L"(no game data)");
  }
}

bool cheats_menu_command(const unsigned int command, CheatOptions &options,
                         std::optional<CheatRequest> &request) {
  const auto values = switches(options);
  if (command >= first_switch && command < first_switch + values.size()) {
    bool &value = *values[command - first_switch];
    value = !value;
    return true;
  }
  if (command == fill_hangars_command) {
    request = CheatRequest{CheatAction::fill_hangars};
    return true;
  }
  if (command == all_items_command) {
    request = CheatRequest{CheatAction::all_items};
    return true;
  }
  if (command >= first_day_command && command <= last_day_command) {
    request = CheatRequest{CheatAction::go_to_day, static_cast<int>(command - first_day_command)};
    return true;
  }
  return false;
}
} // namespace phantomcrash
#endif
