#include "desktop_screens.hpp"
#include "desktop_load_game.hpp"
#include "desktop_game_screens.hpp"
#include "phantomcrash/saved_time.hpp"

namespace phantomcrash {

std::unique_ptr<Screen> ScreenRunner::make_coop_save_load(LoadGameScript script,
                                                          const MenuEntry &entry) {
  const auto data = roots_.document("ui/coop.json");
  const auto *positions = data.find("save_slots_x");
  if (!positions || positions->array().size() != coop_->profiles.size() || script.slots.empty()) {
    throw std::runtime_error("Co-op requires four save slot positions and a save plate");
  }
  const auto plate = script.slots.front();
  script.slots.assign(coop_->profiles.size(), plate);
  std::vector<SaveSlot> slots(script.slots.size());
  for (std::size_t index = 0; index < slots.size(); ++index) {
    auto &style = script.slots[index];
    const float shift =
        static_cast<float>(positions->array()[index].number()) - style.plate.position[0];
    const float down = static_cast<float>(data.number_or("save_slots_y", style.plate.position[1])) -
                       style.plate.position[1];
    for (auto *sprite : {&style.plate, &style.lit, &style.empty, &style.clear}) {
      sprite->position[0] += shift;
      sprite->position[1] += down;
    }
    style.date[0] += shift;
    style.time[0] += shift;
    style.date[1] += down;
    style.time[1] += down;
    slots[index].quest = coop_->saved_profiles[index];
    slots[index].selectable = static_cast<int>(index) == coop_slot_;
    slots[index].owner =
        "P" + std::to_string(index + 1) + (slots[index].selectable ? " - YOUR SAVE" : " - LOCKED");
  }
  SaveLoadHooks hooks;
  hooks.load = [this](const int slot) {
    if (slot == coop_slot_ && coop_->saved_profiles[slot]) {
      SDL_Log("Co-op reload: player=%d current_day=%d saved_day=%d", slot + 1, quest_.day + 1,
              coop_->saved_profiles[slot]->day + 1);
      quest_ = *coop_->saved_profiles[slot];
      quest_.live_epoch = 0;
    }
  };
  hooks.save = [this](const int slot) -> std::optional<QuestSave> {
    if (slot != coop_slot_) {
      return std::nullopt;
    }
    auto checkpoint = quest_;
    checkpoint.saved = local_time_text();
    if (!coop_->save_profile || !coop_->save_profile(slot, checkpoint)) {
      return std::nullopt;
    }
    quest_.saved = checkpoint.saved;
    coop_->profiles[slot] = checkpoint;
    coop_->saved_profiles[slot] = checkpoint;
    return checkpoint;
  };
  return std::make_unique<LoadGameScreen>(textures_, font_, game_->part_tables(), std::move(script),
                                          std::move(slots), message_box_, frame_, credits_, quest_,
                                          entry.hud, std::move(hooks), save_slots_unreadable_);
}

} // namespace phantomcrash
