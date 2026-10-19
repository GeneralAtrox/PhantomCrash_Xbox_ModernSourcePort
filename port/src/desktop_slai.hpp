#pragma once
#include <filesystem>
namespace phantomcrash {
class SettingsMenu;
bool prepare_slai_data(const std::filesystem::path &root, bool interactive);
void update_slai_menu(SettingsMenu &menu, const std::filesystem::path &root,
                      const std::filesystem::path &configuration);
} // namespace phantomcrash
