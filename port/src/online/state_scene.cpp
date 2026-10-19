#include "state_writer.hpp"
#include "phantomcrash/battle_scene.hpp"

namespace phantomcrash {
void StateInspector::write(online::StateWriter &out, const BattleScene &v) {
  // sounds_ and music_ are presentation outboxes. All scene clocks, choices,
  // queued chatter lines and entrance/exit decisions remain part of the digest.
  out(online::remembered_digest(v.tables_,
                                [](online::StateWriter &tables, const BattleSceneTables &value) {
                                  tables(value.states, value.gates);
                                }),
      v.tuning_, v.random_, v.state_, v.left_, v.origin_, v.gate_, v.exit_heading_, v.gate_flag_,
      v.robots_, v.remaining_gates_, v.last_robot_gate_, v.cinematic_, v.boss_played_, v.letterbox_,
      v.letterbox_way_, v.lines_, v.finished_);
}
namespace online {
StateDigest state_digest(const BattleScene &scene) {
  StateWriter out;
  out(std::uint32_t{1}, scene);
  return out.finish();
}
} // namespace online
} // namespace phantomcrash
