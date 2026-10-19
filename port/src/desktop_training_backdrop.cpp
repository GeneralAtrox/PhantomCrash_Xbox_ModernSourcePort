// The battle screen behind the match's receipt (desktop_result_backdrop.hpp):
// the original's RESULT starts a fresh battle of the contest's robots
// (0x04a6a0) on the stage just fought on. The screen keeps its stage, robot
// looks and effects and runs that battle, without the HUD and unheard.

#include "desktop_training.hpp"
#include "desktop_coop_matches.hpp"

#include <exception>
#include <utility>

namespace phantomcrash {
namespace {
// Every slot's weapon, as FreePlay.
constexpr unsigned all_weapons = (1U << weapon_slots) - 1;
} // namespace

int TrainingScreen::backdrop_area() const {
  const bool contest = !vs_ && !finale_;
  return contest && match_results_ && view_ && live_world_ != nullptr ? live_area_ : -1;
}

std::shared_ptr<Battle> TrainingScreen::stand_up_backdrop(RumblingWorld *world,
                                                          const QuestSave &quest) const {
  const int area = backdrop_area();
  if (area < 0) {
    return nullptr;
  }
  if (coop_match_) {
    if (online()) {
      // Standing up the result battle consumes the world's random stream.
      // Its local presentation must not advance the still-running shared world.
      const auto saved = coop_match_->checkpoint();
      try {
        auto fresh = coop_match_->world->stand_up(area, coop_match_->simulation_save);
        coop_match_->restore_checkpoint(*saved);
        return fresh;
      } catch (...) {
        coop_match_->restore_checkpoint(*saved);
        throw;
      }
    }
    return coop_match_->world->stand_up(area, coop_match_->simulation_save);
  }
  return world != nullptr ? world->stand_up(area, quest) : nullptr;
}

bool TrainingScreen::start_backdrop(std::shared_ptr<Battle> fresh,
                                    const std::vector<RobotParts> &looks) {
  if (!fresh || backdrop_area() < 0) {
    return false;
  }
  // The gate doors stand as they are; the area's battle keeps its own.
  if (const StageDoors *doors = battle_->doors()) {
    fresh->set_doors(*doors);
  }
  if (!online()) {
    battle_->set_player_target_view(std::nullopt); // as the destructor leaves it
  }
  battle_ = std::move(fresh);
  player_index_ = 0;
  battle_->select_player(player_index_);
  battle_->take_out(player_index_); // nobody's: seated when this screen was the demo's

  battle_->set_drones_active(true);
  battle_->set_allowed_weapons(all_weapons);
  scene_.reset();
  chatter_.reset();
  supply_.reset();
  message_.reset();
  if (effects_) {
    effects_->system().set_stage(&battle_->stage());
  }
  view_->clear_wrecks();
  if (!looks.empty()) {
    for (std::size_t index = 1; index < looks.size() && index < view_->size(); ++index) {
      try {
        view_->replace(index, looks[index]);
      } catch (const std::exception &) {
        view_->replace(index, RobotParts{}); // as add_live_views
      }
    }
  } else if (coop_match_) {
    // The co-op players' places go back to the contest's own robots.
    for (std::size_t index = 1; index < battle_->robots().size() && index < view_->size();
         ++index) {
      const RumblingRobot *robot = live_world_->robot(live_area_, static_cast<int>(index));
      try {
        view_->replace(index, robot != nullptr ? robot->look : RobotParts{});
      } catch (const std::exception &) {
        view_->replace(index, RobotParts{}); // as add_live_views
      }
    }
  } else {
    // The world may have brought new robots in since the match last drew it.
    follow_live();
  }
  const float fallback = script_.battle.settings.robot_radius;
  for (std::size_t index = 1; index < battle_->robots().size(); ++index) {
    battle_->set_radius(static_cast<int>(index), view_->radius(index, fallback));
  }
  view_->bind(*battle_);
  return true;
}

void TrainingScreen::tick_backdrop(const float seconds) {
  view_->bind(*battle_);
  battle_->tick({}, {}, seconds);
  view_->tick(*battle_, seconds);
  if (shots_) {
    shots_->tick(*battle_, effects_->system(), seconds, view_.get());
  }
  if (point_lights_) {
    point_lights_->tick(*battle_, seconds);
  }
  if (mech_vfx_) {
    mech_vfx_->tick(*battle_, seconds);
  }
  if (effects_) {
    effects_->tick(seconds);
  }
  // Unheard: the receipt has its own music.
  if (StageDoors *doors = battle_->doors()) {
    static_cast<void>(doors->take_sounds());
  }
  if (StageObjects *objects = battle_->objects()) {
    static_cast<void>(objects->take_sounds());
  }
}

std::vector<ReplaySubject> TrainingScreen::backdrop_subjects() const {
  std::vector<ReplaySubject> subjects;
  for (std::size_t index = 0; index < battle_->robots().size(); ++index) {
    subjects.push_back(replay_subject(index));
  }
  return subjects;
}

const StageCollision &TrainingScreen::backdrop_collision() const {
  return battle_->stage();
}

ChaseCameraSettings TrainingScreen::backdrop_chase() const {
  return script_.battle.camera;
}

CameraViewSettings TrainingScreen::backdrop_views() const {
  return script_.battle.views;
}

std::vector<Vec3> TrainingScreen::backdrop_fixed_points() const {
  return views_.fixed_points();
}

WorldFrame TrainingScreen::backdrop_frame(const ChaseCamera &camera, const int width,
                                          const int height) {
  if (stage_.model < 0) {
    WorldFrame empty; // the stage is missing: nothing to stand it on
    empty.width = width;
    empty.height = height;
    return empty;
  }
  return battle_frame(camera, width, height, -1);
}

} // namespace phantomcrash
