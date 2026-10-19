#include "state_writer.hpp"
#include "phantomcrash/battle.hpp"
namespace phantomcrash {
void StateInspector::write(online::StateWriter &out, const StageCollision &v) {
  // A stage's collision never changes once built but holds every triangle.
  out(online::remembered_digest(v.shape_, [](online::StateWriter &shape, const auto &value) {
    shape(value.walls, value.floor, value.grid, value.origin_x, value.origin_z, value.columns,
          value.rows);
  }));
}

void StateInspector::write(online::StateWriter &out, const NavGraph &v) {
  out(v.origin_x_, v.origin_z_, v.cell_, v.columns_, v.rows_, v.cell_first_, v.nodes_, v.edges_);
}

void StateInspector::write(online::StateWriter &out, const StageDoors &v) {
  // sounds_ is a presentation outbox drained independently by local views.
  out(v.doors_, v.tuning_);
}

void StateInspector::write(online::StateWriter &out, const StageObjects &v) {
  // sounds_ does not feed back into gameplay.
  out(v.objects_);
}

void StateInspector::write(online::StateWriter &out, const RobotWrecks &v) {
  // The callback is installed by matched session code/assets; its address and
  // GPU cache are process-local. Its enabled state and generated collision matter.
  out(bool(v.collision_builder_), v.items_);
}

void StateInspector::write(online::StateWriter &out, const SupplyDrop &v) {
  out(v.settings_, v.random_, v.carrier_, v.pickups_, v.route_, v.segment_, v.along_, v.phase_,
      v.hatch_, v.shootable_, v.velocity_, v.wreck_left_, v.crash_, v.bursts_, v.streak_,
      v.special_, v.arrived_, v.robots_, v.rescue_);
}
} // namespace phantomcrash
