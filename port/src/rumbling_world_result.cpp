// The battle behind the match's results: the original's RESULT (0x094fee)
// starts a fresh battle task (0x04a6a0) and stands the contest's robots up in
// it (0x045a68, 0x0456b7), while the area's own battle waits. The attract
// demo (0x0134cd) stands up a contest's roster drawn for it the same way.

#include "phantomcrash/rumbling_world.hpp"
#include "phantomcrash/rumbling_roster.hpp"

#include <algorithm>

namespace phantomcrash {

std::shared_ptr<Battle> RumblingWorld::stand_up(const int area, const QuestSave &quest) {
  if (area < 0 || area >= roster_areas) {
    return nullptr;
  }
  const Area &source = areas_[static_cast<std::size_t>(area)];
  return stand_up_roster(area, source.roster, source.enemy_level_offset, quest, nullptr);
}

std::shared_ptr<Battle> RumblingWorld::stand_up_contest(const int area, const int contest,
                                                        const QuestSave &quest,
                                                        std::vector<RobotParts> &looks) {
  looks.clear();
  if (area < 0 || area >= roster_areas || tables_.contests.contest(contest) == nullptr) {
    return nullptr;
  }
  const AreaRoster roster =
      draw_roster(check_, tables_.contests, quest, contest, [this] { return random_share(); });
  return stand_up_roster(area, roster, 0, quest, &looks);
}

std::shared_ptr<Battle> RumblingWorld::stand_up_roster(const int area, const AreaRoster &roster,
                                                       const int level_offset,
                                                       const QuestSave &quest,
                                                       std::vector<RobotParts> *looks) {
  if (roster.members.empty()) {
    return nullptr;
  }
  const RumblingStage &stage = stages_[static_cast<std::size_t>(area)];
  BattleSettings settings = settings_;
  settings.ai.area = area;
  settings.ai.showcase = true; // quest +0x618c 1
  auto fresh =
      std::make_shared<Battle>(settings, stage.path, static_cast<std::uint32_t>(random_()));
  Battle &battle = *fresh;
  battle.set_stage(stage.collision);
  if (stage.objects) {
    battle.set_objects(*stage.objects);
  }
  // Robot 0 is the player's place, as in the area's battle, so the robots
  // keep the places the screen draws them by.
  battle.add_robot("player", 0, {}, 0.0F, {}, 1.0F);
  battle.take_out(0);
  if (looks != nullptr) {
    looks->assign(1, RobotParts{});
  }
  const std::size_t count =
      std::min(roster.members.size(), static_cast<std::size_t>(result_robots));
  for (std::size_t place = 0; place < count; ++place) {
    static_cast<void>(battle.add_drone("opponent", 1.0F));
    const int index = static_cast<int>(place) + 1;
    const std::string name = "live" + std::to_string(area) + "_" + std::to_string(place);
    RumblingRobot robot =
        seat_member(battle, name, roster.members[place], index, level_offset, quest);
    if (looks != nullptr) {
      looks->push_back(std::move(robot.look));
    }
    scatter(battle, area, index);
  }
  return fresh;
}

} // namespace phantomcrash
