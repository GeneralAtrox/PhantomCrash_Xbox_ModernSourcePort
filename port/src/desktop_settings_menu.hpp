#pragma once

#include "phantomcrash/cheats.hpp"
#include "phantomcrash/settings.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// The window menu: Settings > Visuals opens live display/render controls;
// audio, input controls and Pause When Unfocused remain in Settings.
// Beside Settings, a copy icon copies the game context (desktop_scene_dump.hpp).
// Tools: Level Select, which jumps straight to a screen for testing, and
// Cheats (desktop_cheats_menu.hpp).
// Commands arrive through the window procedure; the game loop reads
// settings() and take_level() each frame. Windows only; elsewhere it holds
// the settings.
class SettingsMenu final {
public:
  SettingsMenu(void *native_window, const Settings &initial);
  ~SettingsMenu();
  SettingsMenu(const SettingsMenu &) = delete;
  SettingsMenu &operator=(const SettingsMenu &) = delete;

  [[nodiscard]] const Settings &settings() const;
  // Keyboard shortcuts change settings through here so the menu stays in step.
  void set_fullscreen(bool enabled);
  bool take_slai_toggle();
  void set_slai_enabled(bool enabled);
  // The menu bar is hidden in fullscreen.
  void set_visible(bool visible);
  // True once after the user interacts with the menu, so the loop can drop
  // the time spent inside the menu instead of catching up.
  bool take_interaction();
  // True while the pointer is on the title or menu bar or a menu is open.
  // Windows draws those with GDI; a tearing flip-model present beside them
  // lets the frame flicker unless something (a recording) forces composition.
  [[nodiscard]] bool frame_active() const;
  [[nodiscard]] bool visuals_open() const;
  // Tools > Level Select's entries, in order.
  void set_levels(const std::vector<std::string> &labels);
  // The entry picked from Level Select since the last call, if any.
  [[nodiscard]] std::optional<int> take_level();
  // True once after the copy icon is clicked.
  [[nodiscard]] bool take_copy();
  // Tools > Cheats > Go to Day's entries, one a calendar day.
  void set_cheat_days(const std::vector<std::string> &labels);
  // The one-off cheat picked since the last call, if any.
  [[nodiscard]] std::optional<CheatRequest> take_cheat();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace phantomcrash
