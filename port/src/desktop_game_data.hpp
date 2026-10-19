#pragma once

#include <filesystem>

namespace phantomcrash {

// Checks the game data before the window opens and brings it up to date by
// running phantomcrash_extract.exe (next to the game) in a console window:
// after an update that changed the converter it converts the assets again
// from the files already extracted, and when the disc has not been extracted
// it asks the player for their disc image first. Returns false when the
// player quits instead. Automated runs (`interactive` false) only log what is
// out of date and never wait on a dialog.
bool prepare_game_data(const std::filesystem::path &root, bool interactive);

} // namespace phantomcrash
