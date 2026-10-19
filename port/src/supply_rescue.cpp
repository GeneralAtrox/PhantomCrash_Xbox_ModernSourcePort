// The rescue (port addition; docs/SUPPLY_BOXES.md): a player's robot low on
// armour is brought a strong repair box by the next carrier, which glides
// down toward it.
#include "phantomcrash/supply_drop.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {

void SupplyDrop::watch(std::vector<Vec3> robots, std::vector<int> rescue) {
  robots_ = std::move(robots);
  rescue_ = std::move(rescue);
}

int SupplyDrop::rescue_kind() const {
  int weakest = -1; // the weakest repair giving at least min_repair
  int strongest = -1;
  for (std::size_t index = 0; index < settings_.kinds.size(); ++index) {
    const SupplyKind &kind = settings_.kinds[index];
    if (kind.effect != SupplyEffect::repair) {
      continue;
    }
    const auto at = static_cast<int>(index);
    if (strongest < 0 ||
        kind.amount > settings_.kinds[static_cast<std::size_t>(strongest)].amount) {
      strongest = at;
    }
    if (kind.amount >= settings_.rescue.min_repair &&
        (weakest < 0 || kind.amount < settings_.kinds[static_cast<std::size_t>(weakest)].amount)) {
      weakest = at;
    }
  }
  return weakest >= 0 ? weakest : strongest;
}

// On loading: the first pickups, one for each robot calling, are its
// rescue boxes, out of the hatch before the others.
void SupplyDrop::load_rescue() {
  if (settings_.rescue.armour_share <= 0.0F || rescue_.empty()) {
    return;
  }
  const int kind = rescue_kind();
  if (kind < 0) {
    return;
  }
  const std::size_t count = std::min(rescue_.size(), pickups_.size());
  for (std::size_t index = 0; index < count; ++index) {
    SupplyPickup &pickup = pickups_[index];
    pickup.kind = kind;
    pickup.target = rescue_[index];
    pickup.spot = -1;
  }
}

// A falling rescue box closes the share of the way across to its robot that
// its fall closes of the way down to the ground under the robot, by at most
// `glide` a tick: it lands at the robot's feet, or as near as it can reach.
void SupplyDrop::glide_to_target(SupplyPickup &pickup, const float ticks,
                                 const std::function<float(Vec3)> &ground) {
  if (pickup.target >= static_cast<int>(robots_.size())) {
    return;
  }
  const Vec3 robot = robots_[static_cast<std::size_t>(pickup.target)];
  const Vec3 target{robot.x, pickup.position.y, robot.z};
  const float drop = pickup.position.y - (ground(target) + settings_.rest_height);
  const float fall = settings_.fall_speed * ticks;
  const float share = drop > fall ? fall / drop : 1.0F;
  float x = (target.x - pickup.position.x) * share;
  float z = (target.z - pickup.position.z) * share;
  const float across = std::sqrt(x * x + z * z);
  const float most = settings_.rescue.glide * ticks;
  if (across > most) {
    x *= most / across;
    z *= most / across;
  }
  pickup.position.x += x;
  pickup.position.z += z;
}

} // namespace phantomcrash
