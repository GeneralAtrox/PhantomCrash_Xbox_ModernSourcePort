// The VS mode's screens of GameScreens: KEEP SV and VS MODE.

#include "desktop_game_screens.hpp"
#include "desktop_keep_sv.hpp"
#include "desktop_vs_mode.hpp"
#include "phantomcrash/saved_time.hpp"

namespace phantomcrash {

// KEEP SV opens afresh from the SYSTEM menu; back from SV SPEC or the
// hangar it keeps its cursor, and from the hangar's KEEP it keeps the
// hangar's Scoobee, stamped with the time now.
std::unique_ptr<Screen> GameScreens::make_keep_sv(const std::string &from,
                                                  const QuestSave &quest) const {
  KeepSvEntry entry;
  entry.fresh = from != keep_sv_.spec_route && from != keep_sv_.make_route;
  if (from == keep_sv_.make_route && kept_hangar_ >= 0) {
    entry.kept = vs_scoobee_from_hangar(quest.garage, kept_hangar_, local_time_text());
  }
  kept_hangar_ = -1;
  KeepSv logic(keep_sv_, parts_, quest, vs_scoobees_, keep_cursor_, std::move(entry));
  return std::make_unique<KeepSvScreen>(textures_, font_, std::move(logic), message_box_, from,
                                        frame_, credits_, quest);
}

// VS MODE opens at its entry; back from its battle at the area select with
// the same players and Scoobees.
std::unique_ptr<Screen> GameScreens::make_vs_mode(const std::string &from,
                                                  const QuestSave &quest) const {
  VsModeEntry entry;
  entry.from_battle = from == vs_mode_.battle_route;
  VsMode logic(vs_mode_, parts_, garage_, vs_scoobees_, vs_session_, entry);
  return std::make_unique<VsModeScreen>(textures_, font_, std::move(logic), message_box_, from,
                                        frame_, credits_, quest);
}

} // namespace phantomcrash
