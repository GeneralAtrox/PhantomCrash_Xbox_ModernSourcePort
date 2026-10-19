// The title's idle demo (original scene 1, 0x0134cd): a fresh battle of a
// contest's robots on a stage of the session's round, built as a RUMBLING
// match's battle screen and turned straight into a backdrop.

#include "desktop_attract_demo.hpp"
#include "desktop_game_screens.hpp"
#include "desktop_screens.hpp"
#include "desktop_training.hpp"
#include "phantomcrash/rumbling_battle.hpp"
#include "phantomcrash/rumbling_world.hpp"

#include <exception>
#include <random>
#include <utility>

namespace phantomcrash {

std::unique_ptr<Screen> ScreenRunner::make_attract_demo() {
  AttractDemoScript script;
  try {
    script = parse_attract_demo(roots_.document("ui/attract_demo.json"));
  } catch (const std::exception &error) {
    SDL_Log("Attract demo: %s", error.what());
    return nullptr;
  }
  RumblingWorld *world = rumbling_world();
  if (world == nullptr) {
    SDL_Log("Attract demo: the RUMBLING world did not load");
    return nullptr;
  }
  const int count = static_cast<int>(script.next_stage.size());
  int index = attract_stage_.value_or(script.first_stage);
  if (index < 0 || index >= count) {
    index = 0;
  }
  // 0x013383 moves the stage on however the demo ends.
  attract_stage_ =
      script.next_stage.empty() ? 0 : script.next_stage[static_cast<std::size_t>(index)];
  const AttractStage stage = attract_stage(index, count);
  const auto pick = std::random_device{}() % script.contests.size();
  const RumblingEntry entry{stage.area, script.contests[pick]};

  std::vector<RobotParts> looks;
  std::shared_ptr<Battle> battle =
      world->stand_up_contest(entry.area, entry.contest, quest_, looks);
  if (!battle) {
    SDL_Log("Attract demo: nobody fits contest %d", entry.contest);
    return nullptr;
  }
  battle_training_ = rumbling_training(training_, test_drive_, rumbling_battle_, entry,
                                       quest_.garage, game_->part_tables(), game_->garage_tables());
  battle_training_.stage =
      world->live().stages[static_cast<std::size_t>(entry.area)][stage.night ? 1 : 0];
  std::unique_ptr<Screen> screen =
      make_training(battle_training_, rumbling_battle_.lesson, battle, entry.area, true);
  auto *training = dynamic_cast<TrainingScreen *>(screen.get());
  if (training == nullptr || !training->start_backdrop(battle, looks)) {
    SDL_Log("Attract demo: the battle screen could not run it");
    return nullptr;
  }
  static_cast<void>(screen.release());
  SDL_Log("Attract demo: stage %d (area %d, %s), contest %d, %zu robots", index, entry.area,
          stage.night ? "night" : "day", entry.contest, looks.empty() ? 0 : looks.size() - 1);
  return std::make_unique<AttractDemoScreen>(textures_, world_.get(), std::move(script),
                                             std::unique_ptr<BackdropBattle>(training));
}

} // namespace phantomcrash
