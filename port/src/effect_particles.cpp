// The particles: where they start (0x060b7c and the shapes at 0x215b18),
// how they fly (0x061d35, 0x063c16) and when they die (0x061b8f).
#include "phantomcrash/effect_system.hpp"

#include "phantomcrash/skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace phantomcrash {

namespace {

constexpr float byte_max = 255.0F;
constexpr float percent = 100.0F;
constexpr float spin_speed = 0.628F;   // 0x3f20d97c: a spinning particle's most per frame
constexpr float debris_spin = 0.0628F; // 0x3d80adfd
constexpr float debris_floor = -3.0F;  // 0x19f894: and is gone below this
constexpr float debris_fall = 3.0F;    // debris falls three times as fast
constexpr float target_reach = 2.0F;   // 0x17ccbc: squared distance that is "there"
constexpr float least_mass = 0.0001F;
constexpr float drag_share = 0.5F;         // 0x17accc
constexpr float offset_share = 0.3F;       // 0x19b550: random_offset's part of push
constexpr float least_random_speed = 0.2F; // 0x3e4ccccd
constexpr float cluster_reach = 0.75F;     // 0x19f8d4
constexpr int immortal = std::numeric_limits<int>::max();

Vec3 turned(const Vec3 v, const Mat4 &m) {
  return {v.x * m.at(0, 0) + v.y * m.at(1, 0) + v.z * m.at(2, 0),
          v.x * m.at(0, 1) + v.y * m.at(1, 1) + v.z * m.at(2, 1),
          v.x * m.at(0, 2) + v.y * m.at(1, 2) + v.z * m.at(2, 2)};
}

// `v` turned `angle` about the unit `axis` (D3DXMatrixRotationAxis).
Vec3 about(const Vec3 v, const Vec3 axis, const float angle) {
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0F - c));
}

// (1, 0, 0) through the turns about x, y and z of `degrees`.
Vec3 aimed(const Vec3 degrees, const float y_radians) {
  const Mat4 turn =
      rotation_x(radians(degrees.x)) * rotation_y(y_radians) * rotation_z(radians(degrees.z));
  return turned({1.0F, 0.0F, 0.0F}, turn);
}

std::uint8_t channel(const float unit) {
  return static_cast<std::uint8_t>(std::clamp(unit, 0.0F, 1.0F) * byte_max);
}

float wrapped_angle(float angle) {
  while (angle < -pi) {
    angle += 2.0F * pi;
  }
  while (angle > pi) {
    angle -= 2.0F * pi;
  }
  return angle;
}

} // namespace

float EffectSystem::curve_time(const EffectEmitter &emitter, const EffectParticle &particle,
                               const CurveTime mode) const {
  switch (mode) {
  case CurveTime::none:
    return 0.0F;
  case CurveTime::age:
    return static_cast<float>(particle.age);
  case CurveTime::emitter_age:
    return static_cast<float>(emitter.frame);
  case CurveTime::life_percent:
    return particle.life > 0 && particle.life != immortal
               ? static_cast<float>(particle.age) * percent / static_cast<float>(particle.life)
               : percent;
  }
  return 0.0F;
}

// 0x0605d5: `direction` tipped by up to `spread` degrees, turned about
// itself at random, at `speed`.
Vec3 EffectSystem::cone(const Vec3 direction, const float spread, float speed,
                        const std::uint32_t flags) {
  Vec3 side = cross(direction + Vec3{1.0F, 1.0F, -1.0F}, direction);
  if (length(side) <= 0.0F) {
    side = cross(Vec3{1.0F, 0.0F, 0.0F} + direction, direction);
  }
  side = normalize(side);
  const float widest = radians(spread);
  const float tip = (flags & effect_flags::exact_spread) != 0 ? widest : random(0.0F, widest);
  const Vec3 out = normalize(about(about(direction, side, tip), direction, random(-pi, pi)));
  if ((flags & effect_flags::random_speed) != 0) {
    speed *= random(least_random_speed, 1.0F);
  }
  return out * speed;
}

// 0x060b7c.
void EffectSystem::spawn(EffectEmitter &emitter, EffectParticle &particle) {
  const EffectDefinition &d = bank_->definitions[static_cast<std::size_t>(emitter.definition)];
  const float fps = bank_->frame_rate;
  const std::uint32_t flags = emitter.flags;
  particle = EffectParticle{};
  particle.alive = true;
  particle.life = static_cast<int>((d.life + random(-1.0F, 1.0F) * d.life_jitter) * fps);
  particle.gravity = emitter.gravity;
  const float shade = random(-d.colour_jitter, d.colour_jitter);
  particle.colour = {channel(d.colour[0] + shade), channel(d.colour[1] + shade),
                     channel(d.colour[2] + shade),
                     static_cast<std::uint8_t>(std::min(
                         static_cast<float>(channel(d.alpha)) + channel(d.alpha_extra), byte_max))};
  particle.size = d.scale_curve.empty() ? d.size + random(-d.size_jitter, d.size_jitter) : 1.0F;
  const float speed = (d.speed + random(-1.0F, 1.0F) * d.speed_jitter) / fps;
  const float spread = d.spread + random(-1.0F, 1.0F) * d.spread_jitter;
  Mat4 frame = emitter.orientation;
  if (d.scale.x != 0.0F || d.scale.y != 0.0F || d.scale.z != 0.0F) {
    frame = scaling(d.scale) * frame;
  }
  Vec3 direction{0.0F, 1.0F, 0.0F};
  const Vec3 up = normalize(turned(direction, frame));
  switch (d.shape) {
  case EffectShape::point:
    particle.position = emitter.position;
    if (d.direction_mode == 0 || d.direction_mode == 1) {
      direction = up;
    }
    break;
  case EffectShape::sphere: {
    const float radius = d.sphere_filled ? random(0.0F, 0.5F) : 0.5F;
    const Vec3 at = turned(
        turned({radius, 0.0F, 0.0F}, rotation_z(random(-pi, pi)) * rotation_y(random(0.0F, pi))),
        frame);
    particle.position = emitter.position + at;
    if (d.direction_mode == 0) {
      direction = d.sphere_filled ? up : normalize(at);
    } else if (d.direction_mode == 1) {
      direction = up;
    }
    break;
  }
  case EffectShape::cluster:
    particle.kind = 2;
    particle.position = emitter.position;
    if (emitter.live == 1) {
      particle.kind = 1; // the centre, kept while any part is left
      particle.life = immortal;
    } else {
      const float reach = particle.size * (random(0.0F, 0.5F) + cluster_reach);
      const float polar = std::acos(random(0.1F, 1.0F));
      const float azimuth = random(-pi, pi);
      const Vec3 out = turned({0.0F, 0.0F, -reach}, rotation_y(polar) * rotation_z(azimuth));
      // 0x063aad stores cluster children relative to their centre. The
      // renderer expands these offsets in view space (0x0bfae4).
      particle.position = out;
    }
    break;
  }
  if ((flags & effect_flags::random_direction) != 0) {
    const float pitch = random(0.0F, pi);
    const float roll = random(-pi, pi);
    const Mat4 turn = rotation_z(roll) * rotation_y(pitch);
    particle.position = emitter.position + turned(turned({d.push, 0.0F, 0.0F}, turn), frame);
  }
  if ((flags & effect_flags::random_offset) != 0) {
    const float pitch = random(0.0F, pi);
    const float roll = random(-pi, pi);
    const Mat4 turn = rotation_z(roll) * rotation_y(pitch);
    particle.position = emitter.position + emitter.spawn_offset +
                        turned(turned({d.push * offset_share, 0.0F, 0.0F}, turn), frame);
  }
  particle.velocity = cone(direction, spread, speed, flags);
  if ((flags & effect_flags::radial_push) != 0) {
    const Vec3 across = particle.velocity - direction * dot(direction, particle.velocity);
    if (length(across) > 0.0F) {
      // 0x060b7c adds this to +0xb8 (position), not +0xc4 (velocity).
      // Treating the spawn offset as per-frame speed flings the fire away.
      particle.position = particle.position + normalize(across) * d.push;
    }
  }
  if ((flags & effect_flags::inherit_velocity) != 0) {
    // 0x061475..0x0614ea writes +0xb8 (position), not +0xc4 (velocity).
    // Distribute births along this frame's emitter path without changing
    // their subsequent speed or streak length.
    particle.position = particle.position - emitter.velocity * random(0.0F, 1.0F);
  }
  if (particle.kind == 0 && (flags & effect_flags::debris_kind) != 0) {
    particle.kind = 9;
  }
  particle.rotation = random(-pi, pi);
  if ((flags & effect_flags::spin) != 0) {
    particle.spin = random(-spin_speed, spin_speed);
  } else if ((flags & effect_flags::debris) != 0) {
    particle.spin = random(-debris_spin, debris_spin);
  }
  particle.previous = particle.position;
  if ((flags & effect_flags::swirl) != 0) {
    // 0x060b7c / 0x063c16 use a birth-time sinusoidal wind weight.
    particle.swirl_yaw = radians(d.wind_angles.y);
    const float phase = static_cast<float>(emitter.frame) * pi;
    particle.swirl_weight = std::sin(particle.swirl_yaw + phase * 0.08F) *
                            std::sin(particle.swirl_yaw * 3.2F + phase * 0.0577F);
  }
  if ((flags & effect_flags::fragments) != 0) {
    spawn_fragments(emitter, particle);
  }
  update(emitter, particle);
}

void EffectSystem::kill(EffectEmitter &emitter, EffectParticle &particle) {
  if (particle.alive) {
    particle.alive = false;
    --emitter.live;
  }
}

// 0x061b8f.
void EffectSystem::update(EffectEmitter &emitter, EffectParticle &particle) {
  move_particle(emitter, particle);
  colour_particle(emitter, particle);
  ++particle.age;
  // The original's INT_MIN birth sentinel becomes INT_MAX on the first
  // update. Keep that one-update grace period: siblings spawn afterwards.
  const bool centre_alone = particle.kind == 1 && particle.age > 1 && emitter.live == 1;
  if ((particle.life != immortal && particle.age > particle.life) || centre_alone) {
    kill(emitter, particle);
    return;
  }
  if (stage_ == nullptr && (emitter.flags & effect_flags::debris) != 0 &&
      particle.position.y < debris_floor) {
    kill(emitter, particle);
    return;
  }
  if ((emitter.flags & effect_flags::die_at_target) != 0) {
    const Vec3 off = particle.position - emitter.target;
    if (dot(off, off) < target_reach) {
      kill(emitter, particle);
    }
  }
}

// 0x061d35: the speed's limits, the motion and the atlas cell.
void EffectSystem::move_particle(EffectEmitter &emitter, EffectParticle &particle) {
  const EffectDefinition &d = bank_->definitions[static_cast<std::size_t>(emitter.definition)];
  const float fps = bank_->frame_rate;
  const float speed = length(particle.velocity);
  float wanted = d.speed_curve.empty() ? speed : d.speed_curve.at(0.0F);
  const float low = d.min_speed_curve.empty()
                        ? d.min_speed
                        : d.min_speed_curve.at(curve_time(emitter, particle, d.min_speed_time));
  const float high = d.max_speed_curve.empty()
                         ? d.max_speed
                         : d.max_speed_curve.at(curve_time(emitter, particle, d.max_speed_time));
  wanted = std::min(std::max(wanted, low / fps), high / fps);
  particle.velocity = speed > 0.0F ? particle.velocity * (wanted / speed) : Vec3{};
  const std::uint32_t flags = emitter.flags;
  if ((flags & effect_flags::fragments) == 0) {
    if ((flags & effect_flags::debris) != 0) {
      move_debris(emitter, particle);
    } else {
      accelerate(emitter, particle);
      particle.position = particle.position + particle.velocity;
      if (particle.age != 0 && (flags & effect_flags::detached) == 0) {
        particle.position = particle.position + emitter.velocity;
      }
    }
  }
  const int per_cell = std::max(1, static_cast<int>(fps / animation_rate));
  particle.cell = d.cell + (d.frames > 0 ? (particle.age / per_cell) % d.frames : 0);
  if (!d.scale_curve.empty()) {
    particle.scale = d.scale_curve.at(curve_time(emitter, particle, d.scale_time));
  }
  particle.rotation = wrapped_angle(particle.rotation + particle.spin);
}

void EffectSystem::colour_particle(const EffectEmitter &emitter, EffectParticle &particle) {
  const EffectDefinition &d = bank_->definitions[static_cast<std::size_t>(emitter.definition)];
  const float colour_time = curve_time(emitter, particle, d.colour_time);
  const EffectCurve *curves[] = {&d.red, &d.green, &d.blue};
  for (std::size_t c = 0; c < 3; ++c) {
    if (!curves[c]->empty()) {
      particle.colour[c] =
          static_cast<std::uint8_t>(std::clamp(curves[c]->at(colour_time), 0.0F, byte_max));
    }
  }
  if (!d.alpha_curve.empty()) {
    const float alpha = d.alpha_curve.at(curve_time(emitter, particle, d.alpha_time));
    particle.colour[3] = static_cast<std::uint8_t>(std::clamp(alpha, 0.0F, byte_max));
  }
}

// 0x063c16: gravity, the pull, the wind and the drag.
void EffectSystem::accelerate(const EffectEmitter &emitter, EffectParticle &particle) {
  const EffectDefinition &d = bank_->definitions[static_cast<std::size_t>(emitter.definition)];
  const float fps = bank_->frame_rate;
  const float time = curve_time(emitter, particle, d.mass_time);
  const float mass = d.mass_curve.empty() ? d.mass : d.mass_curve.at(time);
  const float per_frame = mass * fps * fps;
  const bool debris = (emitter.flags & effect_flags::debris) != 0;
  Vec3 push = debris ? particle.gravity * debris_fall : particle.gravity;
  if (!d.pull_off && per_frame != 0.0F) {
    const float jitter = d.pull_curve.empty() ? d.force_jitter : d.pull_curve.at(time);
    const float strength = d.pull * std::max(random(-1.0F, 1.0F) * jitter + d.force_scale, 0.0F);
    if (!d.pull_to_target) {
      push = push + aimed(d.pull_angles, radians(d.pull_angles.y)) * (strength / per_frame);
    } else {
      // A negative pull draws the particle in (the pickups' to the robot).
      const Vec3 off = particle.position - emitter.target;
      const float distance = length(off);
      if (distance < d.pull_range && distance > 0.0F) {
        push = push + off * (strength / (per_frame * distance));
      }
    }
  }
  if (!d.wind_off && per_frame != 0.0F) {
    const float jitter = d.wind_curve.empty() ? d.drag_jitter : d.wind_curve.at(time);
    const float strength = d.wind * std::max(random(-1.0F, 1.0F) * jitter + d.drag, 0.0F);
    const float yaw = (emitter.flags & effect_flags::random_speed) != 0 ? random(-pi, pi)
                                                                        : radians(d.wind_angles.y);
    if ((emitter.flags & effect_flags::swirl) != 0) {
      push = push + turned({1, 0, 0}, rotation_y(particle.swirl_yaw)) *
                        (strength * particle.swirl_weight / per_frame);
    } else {
      push = push + aimed(d.wind_angles, yaw) * (strength / per_frame);
    }
  }
  particle.velocity = particle.velocity + push;
  const float drag = std::clamp(
      ((random(-1.0F, 1.0F) * d.drag_jitter + d.drag) / std::max(mass, least_mass)) / percent, 0.0F,
      1.0F);
  particle.velocity = particle.velocity * (1.0F - drag * drag_share);
}

} // namespace phantomcrash
