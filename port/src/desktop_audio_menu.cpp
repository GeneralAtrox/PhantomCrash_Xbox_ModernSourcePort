#include "desktop_audio_menu.hpp"

#ifdef _WIN32
#include <windows.h>

#include <array>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {
constexpr unsigned int first_command = 0x7400;
constexpr unsigned int choices = 11;
constexpr unsigned int channels = static_cast<unsigned int>(audio_bus_count) + 1;
constexpr unsigned int reset_command = first_command + channels * choices;
constexpr std::array<const wchar_t *, channels> labels{L"Master",
                                                       L"Music",
                                                       L"Weapons",
                                                       L"Robots",
                                                       L"Environment / destruction",
                                                       L"Pickups",
                                                       L"Interface",
                                                       L"Dialogue / chatter",
                                                       L"Movies"};

int level(const AudioOptions &options, unsigned int channel) {
  return channel == 0 ? options.master : options.levels[channel - 1];
}
} // namespace

void refresh_audio_menu(void *menu, const AudioOptions &options) {
  auto popup = static_cast<HMENU>(menu);
  for (unsigned int channel = 0; channel < channels; ++channel) {
    const int value = level(options, channel);
    const std::wstring label =
        std::wstring(labels[channel]) + L": " + std::to_wstring(value) + L"%";
    MENUITEMINFOW item{sizeof(MENUITEMINFOW)};
    item.fMask = MIIM_STRING;
    item.dwTypeData = const_cast<wchar_t *>(label.c_str());
    SetMenuItemInfoW(popup, channel, TRUE, &item);
    HMENU submenu = GetSubMenu(popup, channel);
    for (unsigned int choice = 0; choice < choices; ++choice) {
      CheckMenuItem(submenu, choice,
                    MF_BYPOSITION |
                        (static_cast<int>(choice * 10) == value ? MF_CHECKED : MF_UNCHECKED));
    }
  }
}

void *create_audio_menu(void *parent, const AudioOptions &options) {
  HMENU popup = CreatePopupMenu();
  if (!popup || !AppendMenuW(static_cast<HMENU>(parent), MF_POPUP,
                             reinterpret_cast<UINT_PTR>(popup), L"&Audio")) {
    if (popup) {
      DestroyMenu(popup);
    }
    throw std::runtime_error("Unable to create audio menu");
  }
  for (unsigned int channel = 0; channel < channels; ++channel) {
    HMENU submenu = CreatePopupMenu();
    if (!submenu ||
        !AppendMenuW(popup, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), labels[channel])) {
      if (submenu) {
        DestroyMenu(submenu);
      }
      throw std::runtime_error("Unable to create audio category menu");
    }
    for (unsigned int choice = 0; choice < choices; ++choice) {
      const std::wstring label = choice == 0 ? L"Mute (0%)" : std::to_wstring(choice * 10) + L"%";
      if (!AppendMenuW(submenu, MF_STRING, first_command + channel * choices + choice,
                       label.c_str())) {
        throw std::runtime_error("Unable to create audio level menu");
      }
    }
  }
  AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(popup, MF_STRING, reset_command, L"Reset audio categories to 100%");
  AppendMenuW(popup, MF_STRING | MF_GRAYED, 0, L"SOUND CONFIG BGM / SE levels also apply");
  refresh_audio_menu(popup, options);
  return popup;
}

bool audio_menu_command(const unsigned int command, AudioOptions &options) {
  if (command == reset_command) {
    options = AudioOptions{};
    return true;
  }
  if (command < first_command || command >= reset_command) {
    return false;
  }
  const unsigned int channel = (command - first_command) / choices;
  const int value = static_cast<int>((command - first_command) % choices) * 10;
  (channel == 0 ? options.master : options.levels[channel - 1]) = value;
  return true;
}
} // namespace phantomcrash
#endif
