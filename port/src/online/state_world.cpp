#include "state_writer.hpp"
#include "phantomcrash/rumbling_world.hpp"

namespace phantomcrash {
void StateInspector::write(online::StateWriter &out, const RumblingWorld &v) {
  // Fixed source tables are covered by the room's executable/content digest.
  // Per-match rules, HUD tuning and every runtime arena/roster are included.
  out(v.check_.rules, v.check_.open_contests, v.tables_.rules, v.tables_.hud, v.settings_,
      v.stages_, v.live_, v.random_, v.epoch_, v.areas_, v.lent_, v.background_, v.deterministic_,
      v.lent_looks_);
}
namespace online {
StateDigest state_digest(const RumblingWorld &world) {
  StateWriter out;
  out(std::uint32_t{1}, world);
  return out.finish();
}
} // namespace online
} // namespace phantomcrash
