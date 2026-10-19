#pragma once

// The headless AI simulator's arena (a research tool, not part of the game):
// the live RUMBLING stood up as the game stands it up (desktop_screens_rumbling.cpp),
// an area's battle lent as a RUMBLING entry lends it, and run as the battle
// screen runs it (60 Hz, robot poses bound, gate doors and supply drops) with
// no window, renderer or audio.

#include "phantomcrash/battle.hpp"
#include "phantomcrash/contests.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/part_stats.hpp"
#include "phantomcrash/robot_library.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/rumbling_battle.hpp"
#include "phantomcrash/rumbling_robot.hpp"
#include "phantomcrash/rumbling_rules.hpp"
#include "phantomcrash/rumbling_world.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/test_drive.hpp"
#include "phantomcrash/training_ground.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace phantomcrash::ai_sim {

// What stands in robot 0's place, the player's.
enum class PlayerMode {
  none,   // out, as when nobody has joined the area
  stand,  // stands where it is put, never fires and never dies
  patrol, // walks the enemy path, never fires and never dies
};

struct ArenaOptions {
  int day{};
  int robots{default_rumble_robots}; // the live RUMBLING's robots an area
  bool poses{true};                  // bind animation poses (fire timing, muzzles)
  bool doors{true};
  bool supply{true};
  bool background{}; // the other areas fight too, as in the game (slower)
  PlayerMode player{PlayerMode::none};
  int player_entrant{-1}; // the stand-in player's robot: an entrant's (-1: a mid-level one)
};

// The tables the world reads; RegulationCheck and RumblingTables refer to
// them, so they outlive every arena.
class SimData {
public:
  explicit SimData(const std::filesystem::path &root);
  SimData(const SimData &) = delete;
  SimData &operator=(const SimData &) = delete;

  DataRoots roots;
  ContestTables contests;
  PartTables parts;
  GarageTables garage;
  RumblingRules rules;
  PartStatsTable part_stats;
  TrainingScript training;
  TestDriveScript test_drive;
  RumblingBattle rumbling_battle;
  std::map<std::string, StageDefinition> stages;
  RumblingLive live;
  BattleHudStyle hud;
  QuestSave new_quest;
  std::unique_ptr<RobotLibrary> robots; // null when the robots are not converted
  std::array<RumblingStage, roster_areas> area_stages{};
  std::array<std::string, roster_areas> area_stage_names{};
};

class SimArena {
public:
  SimArena(SimData &data, const ArenaOptions &options, int area, std::uint32_t seed);
  SimArena(const SimArena &) = delete;
  SimArena &operator=(const SimArena &) = delete;

  // False when the area has nobody out on the day.
  [[nodiscard]] bool ready() const noexcept {
    return battle_ != nullptr;
  }
  // One battle tick: the world brings in replacements, the battle runs.
  void tick(float seconds);

  [[nodiscard]] Battle &battle() {
    return *battle_;
  }
  [[nodiscard]] const RumblingWorld &world() const {
    return *world_;
  }
  [[nodiscard]] int area() const noexcept {
    return area_;
  }
  [[nodiscard]] const std::string &stage_name() const;
  [[nodiscard]] int contest() const noexcept {
    return quest_.rosters[static_cast<std::size_t>(area_)].contest;
  }
  [[nodiscard]] bool has_player() const noexcept {
    return options_.player != PlayerMode::none;
  }
  // The entrant robot `index` (1 and up) is, or -1.
  [[nodiscard]] int entrant(int index) const;

private:
  void seat_player();
  void add_doors_and_supply();
  void bind_pose(int index, const RobotParts &look);
  void follow_world();
  [[nodiscard]] RobotControl player_control();

  SimData &data_;
  ArenaOptions options_;
  int area_{};
  QuestSave quest_;
  RegulationCheck check_;
  std::unique_ptr<RumblingWorld> world_;
  std::shared_ptr<Battle> battle_;
  int changes_{-1};
  std::vector<int> bound_entrants_;
  PathFollower player_path_;
  std::minstd_rand random_;
};

} // namespace phantomcrash::ai_sim
