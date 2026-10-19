#pragma once

// Who is out in a RUMBLING area on a day (the original's team blocks at
// save + 0x2820, 0xdf0 each, rebuilt by 0x06cf5a as each day begins): the
// opponents drawn for the area's contest from its teams (rumbling_roster.hpp).
// The port keeps them fighting while the player is away (rumbling_world.hpp):
// each one's live state goes into the save with it.

#include <array>
#include <cstdint>
#include <vector>

namespace phantomcrash {

// Where an opponent was drawn from (0x045270): a team's member or its
// leader, the contest's champion, or the contest's extra team.
enum class RosterSource { member, leader, champion, extra };

// No eligible replacement: retry only after another opponent leaves the arena.
inline constexpr float waiting_for_opponent = -1.0F;

// An opponent's robot as the live rumble last left it (rumbling_world.hpp).
struct LiveRobot {
  bool placed{};                   // has been out: the rest is its state
  std::array<float, 3> position{}; // on the area's stage
  float heading{};
  float armour{};
  float max_armour{};
  std::array<int, 4> ammo{-1, -1, -1, -1}; // by weapon mount; -1 for a full magazine
  float shield{};                          // the entry shield's seconds left
  bool down{};                             // shot down, its replacement not yet in
  float arrival{};                         // seconds until the replacement comes in
  int kills{};

  bool operator==(const LiveRobot &) const = default;
};

struct RosterMember {
  int entrant{-1}; // tables/town.json entrants; -1 for an empty place
  RosterSource source{RosterSource::member};
  int team{-1}; // tables/town.json teams; the contest for the champion
  int place{};  // 0 the leader, 1 to 8 the members
  LiveRobot live;

  // The same opponent, whatever its live state.
  [[nodiscard]] bool same(const RosterMember &other) const noexcept {
    return entrant == other.entrant && source == other.source && team == other.team &&
           place == other.place;
  }
  bool operator==(const RosterMember &) const = default;
};

struct AreaRoster {
  int day{-1};                       // the quest day it was drawn for; -1 never drawn
  int contest{-1};                   // the contest it was drawn for, or no_contest
  int drawn_for{-1};                 // the contest the teams came from (a cleared game's pick)
  std::vector<RosterMember> members; // the player's place left out
  // First defeats in this day's finite checklist; respawns do not clear them.
  std::vector<int> eliminated;

  bool operator==(const AreaRoster &) const = default;
};

// The areas that have rosters (the original's blocks 0 to 2).
inline constexpr int roster_areas = 3;

// A team's places in the beaten flags (the original's 9 bytes a team at
// save + team x 9): the leader, then the eight members.
inline constexpr int team_places = 9;

[[nodiscard]] constexpr int beaten_key(const int team, const int place) noexcept {
  return team * team_places + place;
}

} // namespace phantomcrash
