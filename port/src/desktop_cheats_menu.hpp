#pragma once

#include "phantomcrash/cheats.hpp"
#include "phantomcrash/settings.hpp"

#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {
// Tools > Cheats (docs/CHEATS.md): four switches kept in the settings, and
// one-off changes to the game the loop takes. Windows only.
void *create_cheats_menu(void *parent);
void refresh_cheats_menu(void *menu, const CheatOptions &options);
// Go to Day's entries, one a calendar day from DAY-01.
void set_cheat_days(void *menu, const std::vector<std::string> &labels);
// True when `command` is the menu's: a switch changes `options`, a one-off
// change sets `request`.
bool cheats_menu_command(unsigned int command, CheatOptions &options,
                         std::optional<CheatRequest> &request);
} // namespace phantomcrash
