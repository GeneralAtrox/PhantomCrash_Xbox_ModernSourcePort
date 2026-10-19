#include "desktop_game_screens.hpp"

#include "desktop_battle_music.hpp"
#include "desktop_chip_shop.hpp"
#include "desktop_dealer.hpp"
#include "desktop_assemble.hpp"
#include "desktop_hangar_menu.hpp"
#include "desktop_module_shop.hpp"
#include "desktop_tune_shop.hpp"
#include "desktop_used_sv.hpp"
#include "desktop_front_end.hpp"
#include "desktop_records.hpp"
#include "desktop_rumbling.hpp"
#include "desktop_schedule.hpp"
#include "desktop_sleep.hpp"
#include "desktop_sonic_amp.hpp"
#include "desktop_sv_spec.hpp"
#include "phantomcrash/added_chips.hpp"

#include <SDL3/SDL_log.h>

namespace phantomcrash {

std::optional<json::JsonValue> converted_table(const DataRoots &roots, const char *relative) {
  if (auto table = read_converted_table(roots, relative)) {
    return table;
  }
  SDL_Log("Table %s is missing; convert the game files again", relative);
  return std::nullopt;
}

GameScreens::GameScreens(TextureCache &textures, const DataRoots &roots, const GlyphFont &font,
                         const FrameStyle &frame, const std::vector<MusicCredit> &credits,
                         const std::vector<StoryEvent> &events)
    : textures_(textures), font_(font), frame_(frame), credits_(credits), events_(events),
      sleep_(parse_sleep_script(roots.document("ui/sleep.json"))),
      schedule_(parse_schedule(roots.document("ui/schedule.json"))),
      records_(parse_records(roots.document("ui/records.json"))),
      battle_music_(parse_battle_music(roots.document("ui/battle_music.json"))),
      sonic_amp_(parse_sonic_amp(roots.document("ui/sonic_amp.json"))),
      dealer_(parse_dealer(roots.document("ui/dealer.json"))),
      used_sv_(parse_used_sv(roots.document("ui/used_sv.json"))),
      buy_chip_(parse_chip_shop(roots.document("ui/chip_shop.json"), ChipShopMode::buy)),
      sell_chip_(parse_chip_shop(roots.document("ui/chip_shop.json"), ChipShopMode::sell)),
      uplift_(parse_chip_shop(roots.document("ui/uplift.json"), ChipShopMode::uplift)),
      buy_module_(parse_module_shop(roots.document("ui/module_shop.json"), ModuleShopMode::buy)),
      sell_module_(parse_module_shop(roots.document("ui/module_shop.json"), ModuleShopMode::sell)),
      stock_module_(parse_stock_module(roots.document("ui/stock_module.json"), sell_module_)),
      customize_(parse_tune_shop(roots.document("ui/customize.json"))),
      hangar_(parse_hangar_menu(roots.document("ui/hangar.json"))),
      assemble_(parse_assemble(roots.document("ui/assemble.json"))),
      sv_spec_(parse_sv_spec(roots.document("ui/sv_spec.json"))),
      robot_preview_(parse_robot_preview(roots.document("ui/robot_preview.json"))),
      rumbling_(parse_rumbling(roots.document("ui/rumbling.json"))),
      rumbling_rules_(parse_rumbling_rules(roots.document("world/rumbling.json"))),
      part_stats_(load_part_stats(roots)),
      message_box_(parse_message_box(roots.document("ui/message_box.json"))),
      keep_sv_(parse_keep_sv(roots.document("ui/keep_sv.json"))),
      vs_mode_(parse_vs_mode(roots.document("ui/vs_mode.json"))) {
  const BattleHudStyle hud = parse_battle_hud_style(roots.document("ui/battle_hud.json"));
  use_reticle_sizes(buy_chip_, hud);
  use_reticle_sizes(sell_chip_, hud);
  use_reticle_sizes(uplift_, hud);
  assemble_.paint_swatches = buy_module_.paint_swatches;
  assemble_.emblem_swatches = buy_module_.emblem_swatches;
  schedule_.skip.sleep = sleep_;
  if (const auto town = converted_table(roots, "tables/town.json")) {
    contests_ = parse_contest_tables(*town);
  }
  if (const auto parts = converted_table(roots, "tables/parts.json")) {
    parts_ = parse_part_tables(*parts);
  }
  if (const auto garage = converted_table(roots, "tables/garage.json")) {
    garage_ = parse_garage_tables(*garage);
  }
  const auto placeholders = roots.document("ui/placeholders.json");
  if (const auto *buttons = placeholders.find("buttons")) {
    placeholder_buttons_ = buttons_from_json(*buttons);
  }
  placeholder_return_ = placeholders.string_or("return_to", "main_menu");
  if (const auto *screens = placeholders.find("screens")) {
    for (const auto &[name, value] : screens->object()) {
      placeholders_[name] = {value.string_or("label", name), value.string_or("music", ""),
                             value.string_or("return_to", placeholder_return_)};
    }
  }
}

// The hangar comes back to the hangar chosen with its menu open on the
// item that left it (0x01eb3a).
std::unique_ptr<Screen> GameScreens::make_hangar(const std::string &from,
                                                 const QuestSave &quest) const {
  int &remembered = dealer_cursors_["hangar"];
  std::optional<HangarAction> resume;
  for (const auto &[action, screen] : hangar_.routes) {
    if (screen == from) {
      resume = action;
    }
  }
  // The screens the hangar's menu opens come back to it and keep its way out.
  if (hangar_.origins.contains(from)) {
    hangar_exit_ = from;
    resume.reset();
  } else if (from == hangar_.exit_to || hangar_exit_.empty()) {
    hangar_exit_ = hangar_.exit_to;
  }
  HangarMenuScript script = hangar_from(hangar_, hangar_exit_);
  script.exit_to = hangar_exit_;
  HangarMenu logic(std::move(script), quest, parts_, resume ? remembered : 0, resume);
  return std::make_unique<HangarMenuScreen>(textures_, font_, std::move(logic), message_box_, from,
                                            frame_, credits_, quest, remembered, preview_world(),
                                            robot_preview_);
}

// RUMBLING comes back from SELECT SV (the hangar) to the area's sub-menu
// (0x016d0e).
std::unique_ptr<Screen> GameScreens::make_rumbling(const std::string &from,
                                                   const QuestSave &quest) const {
  int &remembered = dealer_cursors_["rumbling"];
  std::optional<int> resume;
  if (from == rumbling_.hangar_route) {
    resume = remembered;
  }
  Rumbling logic(rumbling_, contests_, regulation_check(), quest, resume);
  return std::make_unique<RumblingScreen>(textures_, font_, std::move(logic), contests_,
                                          message_box_, from, frame_, credits_, quest, remembered);
}

// ASSEMBLE works on the hangar the hangar's wheel has chosen, coming back
// to the cell that left it (NAME, SPEC).
std::unique_ptr<Screen> GameScreens::make_assemble(const std::string &from,
                                                   const QuestSave &quest) const {
  std::optional<AssembleItem> resume;
  for (const auto &[item, screen] : assemble_.routes) {
    if (screen == from) {
      resume = item;
    }
  }
  Assemble logic(assemble_, quest, parts_, garage_, part_stats_, dealer_cursors_["hangar"], resume);
  return std::make_unique<AssembleScreen>(textures_, font_, std::move(logic), message_box_, from,
                                          frame_, credits_, quest, preview_world(), robot_preview_);
}

// A dealer's offers are its lineup on the day's calendar entry (0x06ce5d).
std::unique_ptr<Screen> GameScreens::make_dealer(const std::string &name, const int index,
                                                 const std::string &from,
                                                 const QuestSave &quest) const {
  int &remembered = dealer_cursors_[name];
  const bool resume = from == "name_entry" || from == dealer_.purchase.spec_screen;
  Dealer logic(dealer_, name, dealer_lineup(index, quest), quest, parts_, garage_, from,
               resume ? remembered : 0);
  return std::make_unique<DealerScreen>(textures_, font_, std::move(logic), message_box_, from,
                                        frame_, credits_, quest, remembered, preview_world(),
                                        robot_preview_);
}

// Wild Machines' used SVs are the day's used_svs lineup (0x01ee3d).
std::unique_ptr<Screen> GameScreens::make_used_sv(const std::string &from,
                                                  const QuestSave &quest) const {
  int &remembered = dealer_cursors_["wild_machines_used_sv"];
  const bool resume = from == "name_entry" || from == used_sv_.purchase.spec_screen;
  UsedSv logic(used_sv_, used_sv_lineup(quest), quest, parts_, garage_, resume ? remembered : 0);
  return std::make_unique<UsedSvScreen>(textures_, font_, std::move(logic), message_box_, from,
                                        frame_, credits_, quest, remembered, preview_world(),
                                        robot_preview_);
}

std::vector<SvOffer> GameScreens::dealer_lineup(const int index, const QuestSave &quest) const {
  int lineup = -1;
  if (!contests_.calendar.empty() && index >= 0 && index < 3) {
    const auto days = static_cast<int>(contests_.calendar.size());
    lineup = contests_.calendar[static_cast<std::size_t>(quest.day % days)]
                 .dealers[static_cast<std::size_t>(index)];
  }
  return dealer_offers(garage_, parts_, lineup, dealer_.price_markup);
}

std::vector<SvOffer> GameScreens::used_sv_lineup(const QuestSave &quest) const {
  int lineup = -1;
  if (!contests_.calendar.empty()) {
    const auto days = static_cast<int>(contests_.calendar.size());
    lineup = contests_.calendar[static_cast<std::size_t>(quest.day % days)].used_svs;
  }
  return used_sv_offers(used_sv_, garage_, parts_, lineup, quest.won_contests);
}

// SV SPEC shows the SV the screen that opened it has chosen: a dealer's or
// the used SVs' offer, KEEP SV's kept Scoobee, or the hangar the hangar's
// wheel has chosen.
std::unique_ptr<Screen> GameScreens::make_sv_spec(const std::string &from,
                                                  const std::string &place_before,
                                                  const QuestSave &quest) const {
  std::optional<std::vector<SvOffer>> offers;
  int chosen = -1;
  if (const auto dealer = dealer_.dealers.find(from); dealer != dealer_.dealers.end()) {
    offers = dealer_lineup(dealer->second.index, quest);
    // Dealer cursors include COMPANY PROFILE before the three SV offers.
    chosen = dealer_cursors_[from] - 1;
  } else if (from == "wild_machines_used_sv") {
    offers = used_sv_lineup(quest);
    chosen = dealer_cursors_[from];
  }
  SpecRobot robot;
  const int kept = keep_cursor_.first + keep_cursor_.row;
  if (from == "keep_sv" && kept >= 0 && kept < static_cast<int>(vs_scoobees_.size())) {
    const VsScoobee &scoobee = vs_scoobees_[static_cast<std::size_t>(kept)];
    robot = record_spec(scoobee.robot, parts_, garage_);
    const Hangar &bay = robot.garage.hangars[static_cast<std::size_t>(robot.hangar)];
    if (const auto chip = robot.garage.stock.find(bay.part(PartSlot::chip));
        chip != robot.garage.stock.end() && !scoobee.chip_name.empty()) {
      chip->second.name = scoobee.chip_name;
    }
  } else if (!offers) {
    robot = hangar_spec(quest.garage, dealer_cursors_["hangar"]);
  } else if (chosen >= 0 && chosen < static_cast<int>(offers->size())) {
    robot = record_spec((*offers)[static_cast<std::size_t>(chosen)].robot, parts_, garage_);
  }
  if (offers) {
    SDL_Log("SV Spec: shop=%s menu=%d offer=%d name=%s", from.c_str(), dealer_cursors_[from],
            chosen, robot.name.c_str());
  }
  SvSpec logic(sv_spec_, std::move(robot), parts_, garage_, part_stats_,
               resolve(sv_spec_.exit_to, place_before));
  return std::make_unique<SvSpecScreen>(textures_, font_, std::move(logic), from, frame_, credits_,
                                        quest, preview_world(), robot_preview_);
}

// BUY CHIP offers the day's chips lineup (0x01efdf) and the bosses' CHIPs the
// player has won that day (add_boss_chips), SELL CHIP and
// PlusTech's UPLIFT the player's CHIPs in stock; both resume at the CHIP chosen after NAME ENTRY.
std::unique_ptr<Screen> GameScreens::make_chip_shop(const std::string &name,
                                                    const ChipShopScript &script,
                                                    const std::string &from,
                                                    const QuestSave &quest) const {
  std::vector<ChipOffer> offers;
  if (script.mode != ChipShopMode::buy) {
    offers = chips_in_stock(parts_, garage_, quest.garage);
  } else if (!contests_.calendar.empty()) {
    const auto days = static_cast<int>(contests_.calendar.size());
    const int lineup = contests_.calendar[static_cast<std::size_t>(quest.day % days)].chips;
    offers = chips_for_sale(parts_, garage_, lineup);
    add_boss_chips(offers, parts_, garage_, contests_, quest.day, quest.won_contests);
  }
  int &remembered = dealer_cursors_[name];
  ChipShop logic(script, std::move(offers), quest, parts_, from == "name_entry" ? remembered : 0);
  return std::make_unique<ChipShopScreen>(textures_, font_, std::move(logic), message_box_, from,
                                          frame_, credits_, quest, remembered);
}

// BUY MODULE offers each category's lineup on the day's calendar entry
// (0x06d3a1); SELL MODULE the player's own.
std::unique_ptr<Screen> GameScreens::make_module_shop(const ModuleShopScript &script,
                                                      const std::string &from,
                                                      const QuestSave &quest) const {
  std::vector<int> lineups;
  if (script.mode == ModuleShopMode::buy && !contests_.calendar.empty()) {
    const auto days = static_cast<int>(contests_.calendar.size());
    const auto &day = contests_.calendar[static_cast<std::size_t>(quest.day % days)];
    lineups.assign(script.categories.size(), -1);
    // The day names a lineup for each category but EXIT, in the grid's order.
    for (std::size_t cell = 0; cell < script.categories.size(); ++cell) {
      if (const auto kind = script.categories[cell].kind) {
        const auto slot = static_cast<std::size_t>(module_lineup_slot(*kind));
        lineups[cell] = slot < day.modules.size() ? day.modules[slot] : -1;
      }
    }
  }
  ModuleShop logic(script, quest, parts_, garage_, part_stats_, std::move(lineups));
  return std::make_unique<ModuleShopScreen>(textures_, font_, std::move(logic), message_box_, from,
                                            frame_, credits_, quest, preview_world(),
                                            robot_preview_);
}

// "back" returns to the place a screen was opened from, past any story event.
std::string GameScreens::resolve(const std::string &target, const std::string &place_before) const {
  if (target != "back") {
    return target;
  }
  return place_before.empty() ? placeholder_return_ : place_before;
}

std::unique_ptr<Screen> GameScreens::make(const std::string &name, const std::string &from,
                                          const std::string &place_before,
                                          const QuestSave &quest) const {
  if (name == "sleep") {
    return std::make_unique<SleepScreen>(textures_.renderer(), sleep_, quest);
  }
  if (name == "schedule") {
    return std::make_unique<ScheduleScreen>(textures_, font_, schedule_, contests_, from,
                                            resolve(schedule_.exit_to, place_before), frame_,
                                            credits_, quest, message_box_);
  }
  if (name == "records") {
    return std::make_unique<RecordsScreen>(textures_, font_, records_, contests_,
                                           story_progress(events_, quest, records_.unscored_events),
                                           from, resolve(records_.exit_to, place_before), frame_,
                                           credits_, quest, preview_world().world);
  }
  if (name == "battle_music") {
    return std::make_unique<BattleMusicScreen>(textures_, font_, battle_music_, from, frame_,
                                               credits_, quest);
  }
  if (name == "sonic_amp") {
    return std::make_unique<SonicAmpScreen>(textures_, font_, sonic_amp_, from, frame_, credits_,
                                            quest);
  }
  if (name == "wild_machines_used_sv") {
    return make_used_sv(from, quest);
  }
  if (name == "wild_machines_buy_chip") {
    return make_chip_shop(name, buy_chip_, from, quest);
  }
  if (name == "wild_machines_sell_chip") {
    return make_chip_shop(name, sell_chip_, from, quest);
  }
  if (name == "plustech_uplift") {
    return make_chip_shop(name, uplift_, from, quest);
  }
  if (name == "wild_machines_buy_module") {
    return make_module_shop(buy_module_, from, quest);
  }
  if (name == "wild_machines_sell_module") {
    return make_module_shop(sell_module_, from, quest);
  }
  if (name == "modules") {
    return make_module_shop(stock_module_, from, quest);
  }
  if (name == "sv_spec") {
    return make_sv_spec(from, place_before, quest);
  }
  if (name == "assemble") {
    return make_assemble(from, quest);
  }
  if (name == "hangar") {
    return make_hangar(from, quest);
  }
  if (name == "rumbling") {
    return make_rumbling(from, quest);
  }
  if (name == "keep_sv") {
    return make_keep_sv(from, quest);
  }
  if (name == "vs_mode") {
    return make_vs_mode(from, quest);
  }
  if (name == "plustech_customize") {
    TuneShop logic(customize_, quest, parts_, garage_, part_stats_);
    return std::make_unique<TuneShopScreen>(textures_, font_, std::move(logic), message_box_, from,
                                            frame_, credits_, quest, preview_world(),
                                            robot_preview_);
  }
  if (const auto look = dealer_.dealers.find(name); look != dealer_.dealers.end()) {
    return make_dealer(name, look->second.index, from, quest);
  }
  if (const auto found = placeholders_.find(name); found != placeholders_.end()) {
    return std::make_unique<PlaceholderScreen>(textures_.renderer(), found->second.label,
                                               found->second.music, placeholder_buttons_,
                                               resolve(found->second.return_to, place_before));
  }
  return nullptr;
}

} // namespace phantomcrash
