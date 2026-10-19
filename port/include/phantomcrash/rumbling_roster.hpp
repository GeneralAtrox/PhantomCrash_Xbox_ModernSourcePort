#pragma once

// Each RUMBLING area's opponents for the day and what WIRE HEADS DATA shows
// of them (data/world/rumbling.json "roster"): the draw from the contest's
// teams (0x045270, 0x045a68, 0x0456b7) and the window's card (0x013f5a).

#include "phantomcrash/area_roster.hpp"
#include "phantomcrash/contests.hpp"
#include "phantomcrash/rumbling_rules.hpp"
#include "phantomcrash/settings.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace phantomcrash {

// A number from 0 up to 1; the draws take one at a time.
using RosterRandom = std::function<float()>;

// 0x046411: whether `entrant` may be out for `contest`: its level in the
// contest's range, and the class's maker, legs, weight and O.C rules met by
// its robot.
[[nodiscard]] bool entrant_fits(const RegulationCheck &check, const ContestTables &contests,
                                int contest, int entrant);

// 0x045270 with 0x045a68 for each place: `area`'s opponents for the
// quest's day at `contest` (none for no_contest).
[[nodiscard]] AreaRoster draw_roster(const RegulationCheck &check, const ContestTables &contests,
                                     const QuestSave &quest, int contest,
                                     const RosterRandom &random);

// The places a contest's roster has, the player's among them: roster_size
// + 1, at most max_size (0x045270), or the live RUMBLING's live_size.
[[nodiscard]] int roster_places(const Contest &contest, const RosterRules &rules);

// The live RUMBLING's next opponent at `roster`'s place `leaving`, once its
// robot is shot down: drawn as the day's roster was, from those not already
// out. First defeats advance a finite checklist, but defeated pilots can
// respawn. The champion is preferred once every pre-boss opponent is marked.
[[nodiscard]] std::optional<RosterMember> draw_replacement(const RegulationCheck &check,
                                                           const ContestTables &contests,
                                                           const QuestSave &quest,
                                                           const AreaRoster &roster, int leaving,
                                                           const RosterRandom &random);

// Every entrant draw_replacement could bring in for `roster`, including
// those already out.
[[nodiscard]] std::vector<int> replacement_entrants(const RegulationCheck &check,
                                                    const ContestTables &contests,
                                                    const QuestSave &quest,
                                                    const AreaRoster &roster);

// Complete finite opponent list, including reserves and leaders, before the boss.
[[nodiscard]] std::vector<RosterMember>
contest_opponents(const RegulationCheck &check, const ContestTables &contests, int contest);

struct RosterStatus {
  std::string name;
  bool eliminated{};
  RosterSource source{RosterSource::member};
  std::string tag; // the pilot's name tag, as its robot's marker shows it
};

// 0x06cf5a: draws each area's roster anew when it is not for today's
// contest; true when any was.
bool refresh_rosters(const RegulationCheck &check, const ContestTables &contests, QuestSave &quest,
                     const RosterRandom &random, bool fill_vacancies = false);

// An opponent as WIRE HEADS DATA shows it (0x013f5a).
struct RosterCard {
  int entrant{-1};
  int level{};
  std::int64_t bounty{}; // NY
};

// 0x0456b7: the entrant's level, raised by a cleared game's laps (0x0466e4),
// and its pilot's bounty at the unraised level.
[[nodiscard]] RosterCard roster_card(const RegulationCheck &check, const ContestTables &contests,
                                     const QuestSave &quest, int entrant, bool raised);

// The cards WIRE HEADS DATA steps through at `area`: the ranker alone in a
// ranker's contest, else the day's roster; empty when there is nobody.
[[nodiscard]] std::vector<RosterCard> wire_heads_cards(const RegulationCheck &check,
                                                       const ContestTables &contests,
                                                       const QuestSave &quest, int area,
                                                       int contest);

} // namespace phantomcrash
