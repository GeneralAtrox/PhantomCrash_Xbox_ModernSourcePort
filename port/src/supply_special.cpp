// The red carrier (port addition; docs/SUPPLY_BOXES.md): earned by
// shooting carriers down before they drop anything, it drops a pickup onto
// each of its spots.
#include "phantomcrash/supply_drop.hpp"

#include <algorithm>

namespace phantomcrash {

// On setting out: the red carrier once the streak is long enough, else the
// original's.
void SupplyDrop::start_special() {
  const SupplySpecial &special = settings_.special;
  special_ = special.streak > 0 && !special.spots.empty() && streak_ >= special.streak;
  if (special_) {
    streak_ = 0;
    arrived_ = true;
  }
  carrier_.scale = special_ ? special.scale : 1.0F;
  carrier_.tint = special_ ? std::optional{special.tint} : std::nullopt;
}

// A falling pickup drifts toward its spot so that it lands there: each tick
// it closes the share of the way across that its fall closes of the way
// down.
void SupplyDrop::steer_to_spot(SupplyPickup &pickup, const float ticks,
                               const std::function<float(Vec3)> &ground) {
  const auto &spots = settings_.special.spots;
  if (pickup.spot >= static_cast<int>(spots.size())) {
    return;
  }
  const auto &spot = spots[static_cast<std::size_t>(pickup.spot)];
  const Vec3 target{spot[0], pickup.position.y, spot[1]};
  const float drop = pickup.position.y - (ground(target) + settings_.rest_height);
  const float fall = settings_.fall_speed * ticks;
  const float share = drop > fall ? fall / drop : 1.0F;
  pickup.position.x += (target.x - pickup.position.x) * share;
  pickup.position.z += (target.z - pickup.position.z) * share;
}

} // namespace phantomcrash
