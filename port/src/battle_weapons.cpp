// The Armory: projectiles in flight, their hits and their blasts.
#include "phantomcrash/battle_weapons.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {

// Port values for the fallback weapons only: one without a record's blast
// damage arms after this long and then goes off when an enemy's body comes
// within this share of the blast radius. Recorded weapons go off where
// they touch, as the original's (0x098eda).
constexpr float fuse_arming_seconds = 0.2F;
constexpr float proximity_fuse_share = 0.4F;
// The steepest a lobbed round leaves.
constexpr float max_launch_degrees = 85.0F;
// Impact flashes (port values).
constexpr float impact_radius = 0.8F;
constexpr float impact_seconds = 0.2F;
constexpr float blast_flash_seconds = 0.6F;
constexpr float strike_radius = 1.5F;
constexpr float strike_seconds = 0.25F;
// 0x09c9c2: a blade touches a robot within its radius times this (0x19cbe8).
constexpr float melee_contact_scale = 1.2F;

// Turns unit vector `from` toward `to` by at most `angle` radians.
Vec3 turn_toward(const Vec3 from, const Vec3 to, const float angle) {
  const float cosine = std::clamp(dot(from, to), -1.0F, 1.0F);
  const float between = std::acos(cosine);
  if (between <= angle || between < 1e-4F) {
    return to;
  }
  const float t = angle / between;
  return normalize(from * (1.0F - t) + to * t);
}

// Unit vector `direction` raised by `degrees` above its own elevation.
Vec3 pitched_up(const Vec3 direction, const float degrees) {
  const Vec3 flat{direction.x, 0.0F, direction.z};
  const float across = length(flat);
  if (across < 1e-4F) {
    return direction;
  }
  const float elevation =
      std::min(std::atan2(direction.y, across) + radians(degrees), radians(max_launch_degrees));
  return flat * (std::cos(elevation) / across) + Vec3{0.0F, std::sin(elevation), 0.0F};
}

// Record blasts hurt every body they reach each tick; the others (port
// tuning) go off at once, hurting less with distance.
bool growing_blast(const WeaponTuning &weapon) {
  return weapon.blast_damage > 0.0F;
}

float blast_radius_at(const WeaponTuning &weapon, const float age) {
  if (weapon.blast_seconds <= 0.0F) {
    return weapon.blast_radius;
  }
  const float grown = std::min(age / weapon.blast_seconds, 1.0F);
  return weapon.blast_start_radius + (weapon.blast_radius - weapon.blast_start_radius) * grown;
}

// The way a hit at `from` pushes a body at `to`: level, away from it.
Vec3 push_direction(const Vec3 from, const Vec3 to) {
  const Vec3 away{to.x - from.x, 0.0F, to.z - from.z};
  return length(away) > 1e-4F ? normalize(away) : Vec3{};
}

// A projectile's level heading.
Vec3 flight_direction(const Vec3 velocity) {
  return push_direction(Vec3{}, velocity);
}

// Hurts every body within `blast`'s radius as it grows from its start
// radius to its full one (0x098298): an explosion hurts each body it
// touches by the blast damage every battle tick (projectile state 3).
void reach(const Blast &blast, const std::vector<Target> &targets, std::vector<WeaponHit> &hits) {
  const WeaponTuning &weapon = *blast.weapon;
  const float radius = blast_radius_at(weapon, blast.age);
  for (std::size_t index = 0; index < targets.size(); ++index) {
    const Target &body = targets[index];
    if (!body.alive || body.team == blast.team) {
      continue;
    }
    if (body.distance_to(blast.position) < radius) {
      hits.push_back({static_cast<int>(index), blast.owner, weapon.blast_damage,
                      weapon.stagger_seconds, body.centre, &weapon,
                      push_direction(blast.position, body.centre), false});
    }
  }
}

// Hurts with `blast` for every battle tick due by its age, while it grows.
void pulse(Blast &blast, const std::vector<Target> &targets, std::vector<WeaponHit> &hits) {
  constexpr float tick = 1.0F / battle_ticks_per_second;
  constexpr float slack = 1e-4F; // float drift in the ages
  while (blast.next_pulse <= blast.age + slack &&
         blast.next_pulse < blast.weapon->blast_seconds - slack) {
    reach(blast, targets, hits);
    blast.next_pulse += tick;
  }
}

} // namespace

Vec3 lob_velocity(const Vec3 from, const Vec3 to, const float speed, const float gravity) {
  Vec3 flat = to - from;
  const float height = flat.y;
  flat.y = 0.0F;
  const float distance = length(flat);
  if (distance < 1e-3F || gravity <= 0.0F) {
    return normalize(to - from) * speed;
  }
  const Vec3 across = flat * (1.0F / distance);
  const float v2 = speed * speed;
  const float root = v2 * v2 - gravity * (gravity * distance * distance + 2.0F * height * v2);
  const float angle = root >= 0.0F ? std::atan((v2 - std::sqrt(root)) / (gravity * distance))
                                   : pi * 0.25F; // out of reach: throw as far as it goes
  return across * (speed * std::cos(angle)) + Vec3{0.0F, speed * std::sin(angle), 0.0F};
}

Vec3 Armory::scatter(const Vec3 direction, const float degrees) {
  if (degrees <= 0.0F) {
    return direction;
  }
  std::uniform_real_distribution<float> unit(0.0F, 1.0F);
  const float off = radians(degrees) * unit(random_);
  const float roll = 2.0F * pi * unit(random_);
  const Vec3 side = std::abs(direction.y) < 0.99F
                        ? normalize(cross(direction, Vec3{0.0F, 1.0F, 0.0F}))
                        : Vec3{1.0F, 0.0F, 0.0F};
  const Vec3 lift = cross(side, direction);
  return normalize(direction * std::cos(off) +
                   (side * std::cos(roll) + lift * std::sin(roll)) * std::sin(off));
}

Vec3 Armory::launch_velocity(const WeaponTuning &weapon, const Vec3 muzzle, const Vec3 direction,
                             const Target *locked) const {
  switch (weapon.kind) {
  case WeaponKind::missile: {
    const float start = weapon.start_speed > 0.0F ? weapon.start_speed : weapon.speed * 0.5F;
    return direction * start;
  }
  case WeaponKind::grenade: {
    // A record's lobbed round leaves at a fixed angle above the aim
    // (0x097ac9); without one, the port lobs it onto the target.
    if (weapon.launch_degrees > 0.0F) {
      return pitched_up(direction, weapon.launch_degrees) * weapon.speed;
    }
    const Vec3 landing =
        locked != nullptr ? locked->centre
                          : muzzle + Vec3{direction.x, 0.0F, direction.z} * (weapon.range * 0.5F);
    return lob_velocity(muzzle, landing, weapon.speed, weapon.gravity);
  }
  case WeaponKind::gun:
    return direction * (weapon.start_speed > 0.0F ? weapon.start_speed : weapon.speed);
  case WeaponKind::melee:
    break;
  }
  return direction * weapon.speed;
}

void Armory::fire(const WeaponTuning &weapon, const int owner, const int team, const Vec3 muzzle,
                  const Vec3 aim, const int target, const std::vector<Target> &targets,
                  const int rounds, const int slot) {
  if (weapon.kind == WeaponKind::melee) {
    return;
  }
  const bool valid = target >= 0 && target < static_cast<int>(targets.size());
  const Target *locked = valid ? &targets[static_cast<std::size_t>(target)] : nullptr;
  // Shoulder aim modes 0/1 follow the crosshair/tracked reticle (0x098bb7).
  // Their pose already supplies that barrel direction; a retained lock outside
  // the sight must not redirect the projectile after the barrel has aimed.
  const bool shoulder = weapon.part && weapon.part->table == WeaponTable::shoulder;
  const bool sight_aimed =
      shoulder && (weapon.aim_mode == 0 || weapon.aim_mode == 1) && !weapon.homing;
  // A shoulder weapon without lock sights (a grenade launcher) fires along
  // its mount, whatever the fire control holds.
  if (shoulder && !lock_sights(weapon)) {
    locked = nullptr;
  }
  Vec3 direction = aim;
  if (locked != nullptr && !sight_aimed) {
    direction = normalize(locked->centre - muzzle);
  }
  for (int round = 0; round < std::max(rounds, 1); ++round) {
    Projectile shot;
    shot.id = next_projectile_id_++;
    shot.weapon = &weapon;
    shot.owner = owner;
    shot.slot = slot;
    shot.team = team;
    shot.position = muzzle;
    shot.target = weapon.kind == WeaponKind::missile && weapon.homing && valid ? target : -1;
    const Vec3 heading = round == 0 ? direction : scatter(direction, weapon.spread_degrees);
    shot.velocity = launch_velocity(weapon, muzzle, heading, locked);
    projectiles_.push_back(shot);
  }
}

bool starts_impact(const WeaponTuning &weapon, const bool on_robot) noexcept {
  const int hit = on_robot ? weapon.robot_hit : weapon.surface_hit;
  return hit == shot_hit::burst || hit == shot_hit::explode || hit == shot_hit::strike ||
         hit == shot_hit::pierce;
}

bool Armory::strike(const WeaponTuning &weapon, const int owner, const int team, const Vec3 blade,
                    const std::vector<Target> &targets, std::vector<WeaponHit> &hits) {
  for (std::size_t index = 0; index < targets.size(); ++index) {
    const Target &victim = targets[index];
    if (static_cast<int>(index) == owner || !victim.alive || !victim.on_robot ||
        victim.team == team ||
        length(victim.centre - blade) >= victim.radius * melee_contact_scale) {
      continue;
    }
    const int target = static_cast<int>(index);
    const Vec3 from = owner >= 0 && owner < static_cast<int>(targets.size())
                          ? targets[static_cast<std::size_t>(owner)].centre
                          : blade;
    hits.push_back({target, owner, weapon.damage, weapon.stagger_seconds, blade, &weapon,
                    push_direction(from, victim.centre), true});
    explosions_.push_back({blade, strike_radius, 0.0F, strike_seconds});
    landings_.push_back({&weapon, owner, blade, true, std::nullopt, -1, target});
    return true;
  }
  return false;
}

void Armory::detonate(const Projectile &shot, const Vec3 at, const std::vector<Target> &targets,
                      std::vector<WeaponHit> &hits, const bool on_robot,
                      std::optional<StageHit> surface, const int target) {
  const WeaponTuning &weapon = *shot.weapon;
  landings_.push_back(
      {&weapon, shot.owner, at, on_robot, surface, shot.slot, on_robot ? target : -1});
  if (weapon.blast_radius <= 0.0F) {
    explosions_.push_back({at, impact_radius, 0.0F, impact_seconds});
    return;
  }
  explosions_.push_back(
      {at, weapon.blast_radius, 0.0F, std::max(weapon.blast_seconds, blast_flash_seconds)});
  if (growing_blast(weapon)) {
    Blast blast{&weapon, shot.owner, shot.team, at};
    if (weapon.blast_seconds > 0.0F) {
      pulse(blast, targets, hits);
      blasts_.push_back(blast);
    } else {
      reach(blast, targets, hits); // a blast that does not grow hurts once
    }
    return;
  }
  for (std::size_t index = 0; index < targets.size(); ++index) {
    const Target &body = targets[index];
    if (!body.alive || body.team == shot.team) {
      continue;
    }
    const float distance = body.distance_to(at);
    if (distance < weapon.blast_radius) {
      const float falloff = 1.0F - std::max(distance, 0.0F) / weapon.blast_radius * 0.5F;
      hits.push_back({static_cast<int>(index), shot.owner, weapon.damage * falloff,
                      weapon.stagger_seconds, body.centre, &weapon, push_direction(at, body.centre),
                      false});
    }
  }
}

void Armory::spread_blasts(const float seconds, const std::vector<Target> &targets,
                           std::vector<WeaponHit> &hits) {
  for (Blast &blast : blasts_) {
    blast.age += seconds;
    pulse(blast, targets, hits);
  }
  std::erase_if(blasts_,
                [](const Blast &blast) { return blast.age >= blast.weapon->blast_seconds; });
}

void Armory::tick(const float seconds, const std::vector<Target> &targets,
                  const StageCollision &stage, std::vector<WeaponHit> &hits,
                  const StageDoors *doors) {
  for (Explosion &flash : explosions_) {
    flash.age += seconds;
  }
  std::erase_if(explosions_, [](const Explosion &flash) { return flash.age >= flash.life; });
  spread_blasts(seconds, targets, hits);
  tick_beams(seconds, targets, stage, hits, doors);

  std::vector<Projectile> flying;
  flying.reserve(projectiles_.size());
  for (Projectile shot : projectiles_) {
    const WeaponTuning &weapon = *shot.weapon;
    shot.age += seconds;
    if (weapon.kind == WeaponKind::missile) {
      const float acceleration = weapon.acceleration > 0.0F ? weapon.acceleration : weapon.speed;
      const float speed = std::min(weapon.speed, length(shot.velocity) + acceleration * seconds);
      Vec3 heading = normalize(shot.velocity);
      // 0x097d42: a missile with a lock homes from its first tick (0x097f1a).
      if (shot.target >= 0 && targets[static_cast<std::size_t>(shot.target)].alive) {
        const Vec3 wanted =
            normalize(targets[static_cast<std::size_t>(shot.target)].centre - shot.position);
        heading = turn_toward(heading, wanted, radians(weapon.turn_degrees_per_second) * seconds);
      }
      shot.velocity = heading * speed;
    } else if (weapon.acceleration > 0.0F) {
      // A straight round speeding up to its top speed (0x09dd85).
      const float speed =
          std::min(weapon.speed, length(shot.velocity) + weapon.acceleration * seconds);
      shot.velocity = normalize(shot.velocity) * speed;
    }
    shot.velocity.y -= weapon.gravity * seconds;
    const Vec3 from = shot.position;
    // A round without a life (a beam) goes no farther than its range.
    float step = length(shot.velocity) * seconds;
    if (weapon.life_seconds <= 0.0F) {
      step = std::min(step, std::max(weapon.range - shot.travelled, 0.0F));
    }
    shot.position = shot.position + normalize(shot.velocity) * step;
    shot.travelled += step;

    // Limit the search by walls/doors, then select the nearest target surface.
    // A vehicle in front of a robot must intercept the shot first.
    auto surface = stage.segment_contact(from, shot.position);
    std::optional<Vec3> impact = surface ? std::optional<Vec3>{surface->position} : std::nullopt;
    if (const auto leaf = doors != nullptr
                              ? doors->segment_hit(from, impact.value_or(shot.position))
                              : std::nullopt) {
      impact = leaf;
      surface.reset();
    }
    int victim = -1;
    const float fuse =
        weapon.blast_radius > 0.0F && !growing_blast(weapon) && shot.age > fuse_arming_seconds
            ? weapon.blast_radius * proximity_fuse_share
            : 0.0F;
    for (std::size_t index = 0; index < targets.size(); ++index) {
      const Target &body = targets[index];
      if (!body.alive || body.team == shot.team) {
        continue;
      }
      if (const auto contact = body.hit_point(from, impact.value_or(shot.position), fuse)) {
        impact = contact;
        victim = static_cast<int>(index);
      }
    }
    bool spent = impact.has_value();
    if (impact) {
      bool on_robot = false;
      if (victim >= 0) {
        const Target &body = targets[static_cast<std::size_t>(victim)];
        on_robot = body.on_robot;
        // Port-tuned blast weapons hurt only through their blast.
        if (weapon.blast_radius <= 0.0F || growing_blast(weapon)) {
          hits.push_back({victim, shot.owner, weapon.damage, weapon.stagger_seconds, *impact,
                          &weapon, flight_direction(shot.velocity), true});
        }
      }
      const std::size_t before = hits.size();
      detonate(shot, *impact, targets, hits, on_robot, victim < 0 ? surface : std::nullopt, victim);
      for (std::size_t index = before; victim >= 0 && index < hits.size(); ++index) {
        hits[index].direct = hits[index].direct || hits[index].victim == victim;
      }
    }
    const bool expired = weapon.life_seconds > 0.0F ? shot.age >= weapon.life_seconds
                                                    : shot.travelled >= weapon.range;
    if (!spent && expired) {
      if (weapon.blast_radius > 0.0F) {
        detonate(shot, shot.position, targets, hits); // blast weapons go off at the end
      }
      spent = true;
    }
    if (!spent) {
      flying.push_back(shot);
    }
  }
  projectiles_ = std::move(flying);
}

void Armory::clear() {
  projectiles_.clear();
  blasts_.clear();
  beams_.clear();
  explosions_.clear();
  landings_.clear();
}

} // namespace phantomcrash
