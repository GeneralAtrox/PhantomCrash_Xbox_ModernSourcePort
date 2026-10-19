#include "desktop_training.hpp"
#include "desktop_online_session.hpp"
#include <chrono>
#include <stdexcept>

namespace phantomcrash {

bool TrainingScreen::online() const {
  return coop_match_ && (coop_match_->network_prepared || coop_match_->online);
}

void TrainingScreen::prepare_online() {
  if (!coop_match_ || coop_match_->online || !free_play_ || screens_ != 1 || vs_ || finale_) {
    throw std::runtime_error("Online preparation requires an unstarted co-op battle");
  }
  if (scene_) {
    coop_match_->rider_scenes.at(coop_slot_) = scene_->shared_scene();
  }
}

void TrainingScreen::capture_online(const RobotControl &control, const FireControl &fire) {
  const double now =
      std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  coop_match_->online->capture(control, fire, local_target_view_, now);
}
} // namespace phantomcrash
