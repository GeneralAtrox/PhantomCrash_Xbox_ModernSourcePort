#include "phantomcrash/supply_drop.hpp"

#include "phantomcrash/skeleton.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {

Vec3 bezier_point(const std::array<Vec3, 4> &segment, const float along) {
  const float t = along;
  const float u = 1.0F - t;
  return segment[0] * (u * u * u) + segment[1] * (3.0F * u * u * t) +
         segment[2] * (3.0F * u * t * t) + segment[3] * (t * t * t);
}

SupplyDrop::SupplyDrop(SupplySettings settings, const std::uint32_t seed)
    : settings_(std::move(settings)), random_(seed) {
  if (active()) {
    start_route();
  }
}

// 0x0679e7 and 0x066ccd: a new route from its start, the hold loaded.
void SupplyDrop::start_route() {
  std::uniform_int_distribution<std::size_t> pick(0, settings_.routes.size() - 1);
  route_ = &settings_.routes[pick(random_)];
  segment_ = 0;
  along_ = 0.0F;
  phase_ = Phase::inbound;
  hatch_ = Hatch::shut;
  carrier_.position = route_->segments.front()[0];
  carrier_.hatch = 0.0F;
  carrier_.parked = false;
  carrier_.wrecked = false;
  shootable_ = true;
  start_special();
  load();
}

// Back at the route's start, out of sight, until the pickups are used up.
void SupplyDrop::park() {
  phase_ = Phase::parked;
  hatch_ = Hatch::shut;
  segment_ = 0;
  along_ = 0.0F;
  carrier_.parked = true;
  carrier_.wrecked = false;
  carrier_.hatch = 0.0F;
  carrier_.position = route_->segments.front()[0];
}

// 0x066ccd, 0x066b91: each pickup carried, the first free to fall at once
// and the others drop_ticks after the one before; each kind rolled. The red
// carrier carries one for each of its spots.
void SupplyDrop::load() {
  const SupplySpecial &special = settings_.special;
  const std::size_t count =
      special_ ? special.spots.size() : static_cast<std::size_t>(settings_.pickups);
  pickups_.assign(count, SupplyPickup{});
  std::uniform_real_distribution<float> roll(0.0F, 1.0F);
  for (std::size_t index = 0; index < pickups_.size(); ++index) {
    SupplyPickup &pickup = pickups_[index];
    const float between = special_ ? special.drop_ticks : settings_.drop_ticks;
    pickup.wait = index == 0 ? 0.0F : between;
    pickup.life = settings_.lifetime_ticks;
    pickup.kind = roll_kind(roll(random_));
    pickup.spot = special_ ? static_cast<int>(index) : -1;
  }
  load_rescue();
}

// The first kind whose running total of chances passes `value`; past them
// all, the first kind with a chance (kind 0 in the original's roll).
int SupplyDrop::roll_kind(const float value) const {
  int fallback = -1;
  float total = 0.0F;
  for (std::size_t index = 0; index < settings_.kinds.size(); ++index) {
    const SupplyKind &kind = settings_.kinds[index];
    const float chance = special_ ? kind.special_chance.value_or(kind.chance) : kind.chance;
    if (chance > 0.0F && fallback < 0) {
      fallback = static_cast<int>(index);
    }
    total += chance;
    if (value < total) {
      return static_cast<int>(index);
    }
  }
  return std::max(fallback, 0);
}

void SupplyDrop::tick(const float seconds, const std::function<float(Vec3)> &ground,
                      const SupplyContact &contact) {
  if (!active() || route_ == nullptr) {
    return;
  }
  const float ticks = seconds * settings_.tick_rate;
  const float spin = settings_.look.pickups && !settings_.look.pickups->carrier_model.empty()
                         ? settings_.look.pickups->light_spin_per_second * seconds
                         : settings_.spin_speed * ticks;
  carrier_.spin = std::fmod(carrier_.spin + spin, 2.0F * pi);
  if (phase_ == Phase::wreck) {
    fall(ticks, ground, contact);
  } else if (phase_ != Phase::parked) {
    fly(ticks);
  }
  if (phase_ == Phase::dropping) {
    work_hatch(ticks);
  }
  tick_pickups(ticks, ground);
  if (phase_ == Phase::parked) {
    // 0x0683b6, 0x06a2f6: once no pickup is out, out again (those still
    // in a shot-down carrier's hold are lost with it).
    const bool none_out = std::ranges::all_of(pickups_, [](const SupplyPickup &pickup) {
      return pickup.state == SupplyState::gone || pickup.state == SupplyState::carried;
    });
    if (none_out) {
      start_route();
    }
  }
}

// 0x067bf6, 0x067f04 and 0x06804b move the carrier along its segments; the
// boundaries change what it does.
void SupplyDrop::fly(const float ticks) {
  const Vec3 before = carrier_.position;
  along_ += settings_.speed * ticks;
  while (along_ > 1.0F && phase_ != Phase::parked) {
    along_ -= 1.0F;
    ++segment_;
    if (segment_ >= static_cast<int>(route_->segments.size())) {
      if (phase_ == Phase::dropping) {
        segment_ = 0; // 0x06804b wraps round while it drops
        continue;
      }
      streak_ = 0; // it flew its route without being brought down
      park();
      return;
    }
    if (phase_ == Phase::inbound && segment_ == route_->closing) {
      phase_ = Phase::closing;
    } else if (phase_ == Phase::closing && segment_ == route_->drop) {
      phase_ = Phase::dropping;
      hatch_ = Hatch::opening;
    } else if (phase_ == Phase::closing && segment_ >= route_->leave) {
      phase_ = Phase::outbound;
    }
  }
  carrier_.position =
      bezier_point(route_->segments[static_cast<std::size_t>(segment_)], std::min(along_, 1.0F));
  const Vec3 moved = carrier_.position - before;
  if (ticks > 0.0F) {
    velocity_ = moved * (1.0F / ticks);
  }
  if (moved.x * moved.x + moved.z * moved.z > 1e-8F) {
    carrier_.heading = std::atan2(moved.x, moved.z);
  }
}

// 0x06804b: open the hatch, let the pickups fall one by one, close it.
void SupplyDrop::work_hatch(const float ticks) {
  const float step = settings_.hatch_speed * ticks;
  switch (hatch_) {
  case Hatch::shut:
    break;
  case Hatch::opening:
    carrier_.hatch += step;
    if (carrier_.hatch > settings_.hatch_open) {
      carrier_.hatch = settings_.hatch_open;
      hatch_ = Hatch::dropping;
    }
    break;
  case Hatch::dropping: {
    for (SupplyPickup &pickup : pickups_) {
      if (pickup.wait > 0.0F) {
        pickup.wait -= ticks;
        break;
      }
      if (pickup.state == SupplyState::carried) {
        pickup.state = SupplyState::falling;
      }
    }
    const bool all_out = std::ranges::none_of(
        pickups_, [](const SupplyPickup &pickup) { return pickup.state == SupplyState::carried; });
    if (all_out) {
      hatch_ = Hatch::closing;
    }
    break;
  }
  case Hatch::closing:
    carrier_.hatch -= step;
    if (carrier_.hatch < 0.0F) {
      carrier_.hatch = 0.0F;
      hatch_ = Hatch::shut;
      phase_ = Phase::closing; // back to 0x067f04, which turns it away
    }
    break;
  }
}

// 0x06781a: a shot brings the flying carrier down; it keeps part of its
// speed and sinks.
bool SupplyDrop::shoot_carrier() {
  if (!carrier_shootable()) {
    return false;
  }
  const bool none_dropped = std::ranges::all_of(
      pickups_, [](const SupplyPickup &pickup) { return pickup.state == SupplyState::carried; });
  streak_ = none_dropped && !special_ ? streak_ + 1 : 0;
  shootable_ = false;
  carrier_.wrecked = true;
  phase_ = Phase::wreck;
  velocity_ = velocity_ * settings_.wreck_keep;
  velocity_.y = -settings_.wreck_sink;
  wreck_left_ = settings_.wreck_ticks;
  hatch_ = Hatch::shut;
  // Cargo still in the hold is destroyed. Already released pickups remain
  // independent of the wreck and continue falling or waiting on the ground.
  for (SupplyPickup &pickup : pickups_) {
    if (pickup.state == SupplyState::carried) {
      pickup.state = SupplyState::gone;
    }
  }
  return true;
}

// 0x06828e: the wreck falls until it strikes the ground or its time is up,
// and blows up.
void SupplyDrop::fall(const float ticks, const std::function<float(Vec3)> &ground,
                      const SupplyContact &contact) {
  const Vec3 before = carrier_.position;
  carrier_.position = carrier_.position + velocity_ * ticks;
  wreck_left_ -= ticks;
  const auto hit = contact ? contact(before, carrier_.position, carrier_radius()) : std::nullopt;
  if (hit || carrier_.position.y - carrier_radius() <= ground(carrier_.position) ||
      wreck_left_ < 0.0F) {
    crash_ = hit.value_or(carrier_.position);
    park();
  }
}

void SupplyDrop::shoot_pickup(const std::size_t index) {
  if (index < pickups_.size() && (pickups_[index].state == SupplyState::falling ||
                                  pickups_[index].state == SupplyState::landed)) {
    pickups_[index].state = SupplyState::burst;
  }
}

Mat4 SupplyDrop::carrier_frame() const {
  const float scale = carrier_.scale;
  return scaling({scale, scale, scale}) * rotation_y(carrier_.heading) *
         translation(carrier_.position);
}

// 0x069d52: carried pickups ride in the hold; falling ones drop to rest
// above the ground; landed ones wait out their lifetime and burst.
void SupplyDrop::tick_pickups(const float ticks, const std::function<float(Vec3)> &ground) {
  const Mat4 frame = carrier_frame();
  for (SupplyPickup &pickup : pickups_) {
    switch (pickup.state) {
    case SupplyState::carried:
      pickup.position = transform_point(settings_.hold, frame);
      break;
    case SupplyState::falling: {
      if (pickup.target >= 0) {
        glide_to_target(pickup, ticks, ground);
      } else if (pickup.spot >= 0) {
        steer_to_spot(pickup, ticks, ground);
      }
      pickup.position.y -= settings_.fall_speed * ticks;
      const float rest = ground(pickup.position) + settings_.rest_height;
      if (pickup.position.y < rest) {
        pickup.position.y = rest;
        pickup.state = SupplyState::landed;
      }
      break;
    }
    case SupplyState::landed:
      pickup.life -= ticks;
      if (pickup.life < 0.0F) {
        pickup.state = SupplyState::burst;
      }
      break;
    case SupplyState::burst:
      bursts_.push_back(pickup.position); // 0x069d52 starts group 0x40 at unit scale
      pickup.state = SupplyState::gone;
      break;
    case SupplyState::gone:
      break;
    }
    if (pickup.state == SupplyState::falling || pickup.state == SupplyState::landed) {
      if (settings_.look.pickups && settings_.look.pickups->pulse) {
        const auto &pulse = *settings_.look.pickups->pulse;
        const float frames = pickup.life * pulse.frame_rate / settings_.tick_rate;
        const float rate =
            frames <= pulse.fast_frames ? pulse.fast_rate : pulse.life_numerator / frames;
        pickup.blink = std::fmod(
            pickup.blink + pulse.step * rate * ticks * pulse.frame_rate / settings_.tick_rate, pi);
        continue;
      }
      const float rate = pickup.life <= settings_.blink_fast_ticks
                             ? settings_.blink_fast
                             : settings_.lifetime_ticks / pickup.life;
      pickup.blink = std::fmod(pickup.blink + settings_.blink_step * rate * ticks, pi);
    }
  }
}

std::size_t pick_ammo_slot(const std::vector<float> &shares, const bool emptiest,
                           std::minstd_rand &random) {
  if (!emptiest) {
    std::uniform_int_distribution<std::size_t> pick(0, shares.size() - 1);
    return pick(random);
  }
  return static_cast<std::size_t>(std::ranges::min_element(shares) - shares.begin());
}

float SupplyDrop::blink(const SupplyPickup &pickup) {
  return std::fabs(std::sin(pickup.blink));
}

std::optional<SupplyTaken> SupplyDrop::take(const Vec3 centre, const float radius) {
  for (SupplyPickup &pickup : pickups_) {
    const bool out = pickup.state == SupplyState::falling || pickup.state == SupplyState::landed;
    if (out && length(pickup.position - centre) < radius + settings_.reach) {
      pickup.state = SupplyState::gone;
      return SupplyTaken{settings_.kinds[static_cast<std::size_t>(pickup.kind)], pickup.kind,
                         pickup.position};
    }
  }
  return std::nullopt;
}

} // namespace phantomcrash
