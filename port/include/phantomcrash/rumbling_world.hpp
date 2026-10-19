#pragma once

// The live RUMBLING, a port addition (data/world/rumbling.json "live"): each
// area's opponents fight on its stage whether the player is there or not.
// The original only draws the day's roster (rumbling_roster.hpp) and starts
// the battle afresh; here the battle goes on from a NEW GAME or a loaded game
// until the day ends. One shot down leaves and the next is drawn to walk in;
// entering the area joins its battle as it stands. The opponents' state goes
// into the save with the rosters (LiveRobot).

#include "phantomcrash/area_roster.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/enemy_path.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/rumbling_robot.hpp"
#include "phantomcrash/rumbling_roster.hpp"
#include "phantomcrash/stage_collision.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace phantomcrash {

// The robots the match's results stand up behind the receipt: 0x045a68
// fills up to six slots from the contest's roster (an empty one is 0xfffe).
inline constexpr int result_robots = 6;

struct RumblingLive {
  double tick_hz{30.0};        // the areas' battles run at this rate, off screen
  float arrival_seconds{8.0F}; // an opponent shot down is replaced this long after
  // Each area's stage by day and by night (data/world/stages.json names).
  std::array<std::array<std::string, 2>, roster_areas> stages{};
  // Screens the world does not run on: those before a game is under way.
  std::vector<std::string> paused_screens;
  // Screens on which the areas but the player's fight (the area choice, whose
  // map shows them); elsewhere they wait. Absent, they fight on every screen.
  std::optional<std::vector<std::string>> fighting_screens;
  DroneAi ai;
};

// The "live" member of rumbling.json; throws std::runtime_error naming the
// member at fault.
[[nodiscard]] RumblingLive parse_rumbling_live(const json::JsonValue &document);

// An area's stage as its battle needs it.
struct RumblingStage {
  EnemyPath path;
  StageCollision collision;
  std::optional<StageObjects> objects;
};

class RumblingWorld final {
  friend class StateInspector;

public:
  struct Checkpoint;
  // In-memory rollback for this world, including every arena and RNG stream.
  // Static tables and RobotLibrary resources still belong to the session.
  [[nodiscard]] std::shared_ptr<const Checkpoint> checkpoint() const;
  void restore_checkpoint(const Checkpoint &state);
  void prepare_deterministic();

  // `settings` are the battle's (the training ground's, whose weapons the
  // opponents' start from); the tables `check` and `tables` refer to must
  // outlive the world.
  RumblingWorld(RegulationCheck check, RumblingTables tables, BattleSettings settings,
                std::array<RumblingStage, roster_areas> stages, RumblingLive live,
                std::uint32_t seed);

  // Draws the day's rosters when due, takes up rosters it did not write (a
  // new day's, a new or loaded game's), runs each area but a lent one for
  // `seconds` and writes their state back into `quest`.
  void update(QuestSave &quest, double seconds);
  // Area `area`'s battle for the player to join: robot 0 is the player's
  // place, out of the battle until they take it. The world stops running
  // it but still brings in the next opponents, until it is given back.
  // Null when the area has nobody out.
  [[nodiscard]] std::shared_ptr<Battle> lend(int area, QuestSave &quest);
  // The player has left: the opponents they shot down are beaten and their
  // bounties paid.
  std::int64_t give_back(QuestSave &quest);
  // A fresh battle of area `area`'s roster behind the match's results
  // (0x04a6a0, 0x045a68): robot 0 is the player's place, out, and up to
  // result_robots of the roster's robots stand at full armour on the path.
  // The world does not run it or keep it. Null when the area has nobody out.
  [[nodiscard]] std::shared_ptr<Battle> stand_up(int area, const QuestSave &quest);
  // The attract demo's battle (0x0134cd): contest `contest`'s roster drawn
  // afresh (0x045270, 0x045a68) and stood up as stand_up does, on area
  // `area`'s stage. `looks` gets each battle robot's look, robot 0's empty.
  // Null when nobody fits the contest.
  [[nodiscard]] std::shared_ptr<Battle>
  stand_up_contest(int area, int contest, const QuestSave &quest, std::vector<RobotParts> &looks);
  [[nodiscard]] bool player_won() const;
  [[nodiscard]] std::vector<RosterStatus> roster_status(int area) const;
  // The bounties of the opponents the player has shot down in the lent area
  // so far, for the HUD; give_back pays them.
  [[nodiscard]] std::int64_t bounty_won() const;
  [[nodiscard]] std::int64_t coop_bounty(const QuestSave &quest, int entrant) const;
  void release_coop_robot(int area, int actor);
  void set_coop_level_offset(int area, int offset, const QuestSave &quest);
  [[nodiscard]] int lent() const noexcept {
    return lent_;
  }
  // Whether the areas other than the lent one go on fighting (the game's
  // default). The headless AI simulator watches only the lent area's battle.
  void set_background(const bool run) noexcept {
    background_ = run;
  }
  // Whether the areas but the lent one fight at all just now (the screen's
  // choice, RumblingLive::fighting_screens); while not, downed opponents are
  // still replaced.
  void set_fighting(const bool fight) noexcept {
    fighting_ = fight;
  }
  // Battle robot `index`'s opponent at `area` (roster place index - 1), or null.
  [[nodiscard]] const RumblingRobot *robot(int area, int index) const;
  // Counts each time one of the area's robots becomes someone else.
  [[nodiscard]] int changes(int area) const;
  // The looks of everyone who may come in at the lent area, as it was lent:
  // their robots can be loaded before they are needed.
  [[nodiscard]] const std::vector<RobotParts> &lent_looks() const noexcept {
    return lent_looks_;
  }
  [[nodiscard]] const RumblingLive &live() const noexcept {
    return live_;
  }

private:
  struct Area {
    std::shared_ptr<Battle> battle;
    AreaRoster roster;
    std::vector<RumblingRobot> robots; // by roster place
    double clock{};
    int changes{};
    int enemy_level_offset{};
    std::vector<RosterMember> player_kills; // since it was lent
    std::int64_t player_bounty{};           // their bounties, paid on giving it back
  };

  void build(int area, const QuestSave &quest);
  // Puts roster place `place`'s robot into robot `place` + 1.
  void fit(int area, std::size_t place, const QuestSave &quest);
  // Arms and names `battle`'s robot `place` + 1 as roster place `place`'s.
  RumblingRobot seat(Battle &battle, int area, std::size_t place, const QuestSave &quest);
  RumblingRobot seat_member(Battle &battle, const std::string &name, const RosterMember &member,
                            int robot_index, int level_offset, const QuestSave &quest);
  std::shared_ptr<Battle> stand_up_roster(int area, const AreaRoster &roster, int level_offset,
                                          const QuestSave &quest, std::vector<RobotParts> *looks);
  // Stands a new opponent somewhere on the area's path.
  void scatter(Battle &battle, int area, int index);
  void restore(Battle &battle, int index, const LiveRobot &live);
  // Whether it changed the area's battle.
  bool step(int area, const QuestSave &quest, float seconds, bool run);
  static void record(const Combatant &robot, LiveRobot &live);
  [[nodiscard]] float random_share();

  RegulationCheck check_;
  RumblingTables tables_;
  BattleSettings settings_;
  std::array<RumblingStage, roster_areas> stages_;
  RumblingLive live_;
  std::minstd_rand random_;
  std::int64_t epoch_{};
  std::array<Area, roster_areas> areas_{};
  int lent_{-1};
  bool background_{true};
  bool fighting_{true};
  bool deterministic_{}; // online worlds prepare navigation before any arena starts
  std::vector<RobotParts> lent_looks_;
  // Each idle area's battle as last saved or restored, while the world has not
  // changed it since: a checkpoint shares that copy rather than cloning an
  // unchanged battle again (rumbling_checkpoint.cpp). Not simulation state.
  struct SavedBattle {
    const Battle *of{};
    std::shared_ptr<const Battle> copy;
  };
  mutable std::array<SavedBattle, roster_areas> saved_battles_{};
};

} // namespace phantomcrash
