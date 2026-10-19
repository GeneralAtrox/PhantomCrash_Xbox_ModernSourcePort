// The menu screens of ScreenRunner: OPTION and its sub-screens, LOAD GAME
// and the garage's system menu, which is an OPTION-style list.

#include "desktop_screens.hpp"

#include "desktop_load_game.hpp"
#include "desktop_game_screens.hpp"
#include "desktop_option.hpp"
#include "desktop_option_screens.hpp"

#include "phantomcrash/saved_time.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {

MenuEntry ScreenRunner::entered(const std::string &name, const MenuBasics &basics,
                                const std::string &from) {
  // Coming back from one of its own screens (SOUND CONFIG, say) keeps where
  // the menu was first entered from.
  if (from.empty() || from == basics.exit_to || basics.entered_from.contains(from)) {
    menu_origins_[name] = from;
  }
  return menu_entry(basics, menu_origins_[name]);
}

std::unique_ptr<Screen> ScreenRunner::make_menu(const std::string &name, const std::string &from) {
  if (name == "option" || name == "system") {
    const OptionScript &base = name == "option" ? option_ : system_;
    const MenuEntry entry = entered(name, base, from);
    OptionScript script = base;
    script.exit_to = entry.exit_to;
    return std::make_unique<OptionScreen>(textures_, font_, std::move(script), message_box_,
                                          options_, frame_, credits_, from, quest_, entry.hud);
  }
  if (name == "sound_config") {
    return std::make_unique<SoundConfigScreen>(textures_, font_, sound_config_, options_, frame_,
                                               credits_);
  }
  if (name == "button_config") {
    return std::make_unique<ButtonConfigScreen>(textures_, font_, button_config_, options_, frame_,
                                                credits_);
  }
  if (name == "screen_adjust") {
    return std::make_unique<ScreenAdjustScreen>(textures_, font_, screen_adjust_, message_box_,
                                                options_);
  }
  if (name == "load_game" || name == "save_game") {
    return make_save_load(name, from);
  }
  return nullptr;
}

// LOAD GAME and SAVE share the slots: SAVE writes the game kept now, stamped
// with the time, and LOAD GAME makes a slot's game the one kept. Before
// anything is saved, LOAD GAME's first slot holds the kept game.
std::unique_ptr<Screen> ScreenRunner::make_save_load(const std::string &name,
                                                     const std::string &from,
                                                     const QuestSave *live) {
  const LoadGameScript &base = name == "save_game" ? save_game_ : load_game_;
  // Over a screen (the HUD's SAVE button), EXIT goes back to it, with the
  // day and money shown, and SAVE's usual way back is left as it was.
  const MenuEntry entry = live != nullptr ? MenuEntry{from, true} : entered(name, base, from);
  LoadGameScript script = base;
  script.exit_to = entry.exit_to;
  if (cooperative()) {
    return make_coop_save_load(std::move(script), entry);
  }
  std::vector<SaveSlot> slots(script.slots.size());
  const bool none = std::ranges::none_of(
      save_slots_, [](const std::optional<QuestSave> &slot) { return slot.has_value(); });
  for (std::size_t index = 0; index < slots.size() && index < save_slots_.size(); ++index) {
    slots[index].quest = save_slots_[index];
  }
  if (none && !script.saving && !quest_.player_name.empty() && !slots.empty()) {
    slots.front().quest = quest_;
  }
  SaveLoadHooks hooks;
  hooks.load = [this](const int slot) {
    if (slot >= 0 && slot < static_cast<int>(save_slots_.size()) &&
        save_slots_[static_cast<std::size_t>(slot)]) {
      quest_ = *save_slots_[static_cast<std::size_t>(slot)];
      quest_.live_epoch = 0; // the live RUMBLING takes up the save's opponents
    }
  };
  hooks.save = [this, live = live != nullptr ? std::optional{*live} : std::nullopt](
                   const int slot) mutable -> std::optional<QuestSave> {
    if (slot < 0 || slot >= static_cast<int>(save_slots_.size())) {
      return std::nullopt;
    }
    QuestSave &saving = live ? *live : quest_;
    saving.saved = local_time_text();
    save_slots_[static_cast<std::size_t>(slot)] = saving;
    return saving;
  };
  return std::make_unique<LoadGameScreen>(textures_, font_, game_->part_tables(), std::move(script),
                                          std::move(slots), message_box_, frame_, credits_,
                                          live != nullptr ? *live : quest_, entry.hud,
                                          std::move(hooks), save_slots_unreadable_);
}

} // namespace phantomcrash
