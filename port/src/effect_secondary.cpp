#include "phantomcrash/effect_system.hpp"
#include "phantomcrash/stage_collision.hpp"

#include <algorithm>

namespace phantomcrash {

void EffectSystem::spawn_fragments(EffectEmitter &emitter, EffectParticle &seed) {
  const auto &tuning = bank_->particle_motion;
  const auto &definition = bank_->definitions[emitter.definition];
  const Mat4 frame = scaling(definition.scale) * emitter.orientation;
  // An arc's chain spans its segment: the seed and its copies share it out.
  const Vec3 along = length(emitter.segment) > 0.0F
                         ? emitter.segment * (1.0F / static_cast<float>(tuning.fragment_copies + 1))
                         : transform_point({0, tuning.fragment_step, 0}, frame);
  const float jitter = tuning.fragment_jitter * definition.push;
  const Vec3 side = transform_point({jitter, 0, 0}, frame);
  const Vec3 across = transform_point({0, 0, jitter}, frame);
  const Vec3 origin = seed.position;
  seed.velocity = along;
  seed.position = seed.previous = origin + along;
  Vec3 previous_jitter{};
  for (int copy = 0; copy < tuning.fragment_copies && emitter.live <= particle_limit; ++copy) {
    auto free = std::find_if(emitter.particles.begin(), emitter.particles.end(),
                             [](const EffectParticle &particle) { return !particle.alive; });
    if (free == emitter.particles.end()) {
      break;
    }
    // 0x061755: copies 2..9 each jitter about their own segment position;
    // jitter must not accumulate. The previous offset supplies the streak
    // vector, while the new offset supplies this copy's position.
    *free = seed;
    free->velocity = along + previous_jitter;
    const float side_offset = random(-1, 1);
    const float across_offset = random(-1, 1);
    previous_jitter = side * side_offset + across * across_offset;
    free->position = free->previous =
        origin + along * static_cast<float>(copy + 2) + previous_jitter;
    free->rotation = random(-pi, pi);
    ++emitter.live;
    update(emitter, *free);
  }
}

void EffectSystem::move_debris(EffectEmitter &emitter, EffectParticle &particle) {
  if (particle.resting) {
    return;
  }
  accelerate(emitter, particle);
  const auto &tuning = bank_->particle_motion;
  const Vec3 before = particle.position;
  Vec3 after = before + particle.velocity;
  if (particle.age != 0 && (emitter.flags & effect_flags::detached) == 0) {
    after = after + emitter.velocity;
  }
  std::optional<StageHit> hit;
  if (stage_ && tuning.debris_stage_contacts) {
    hit = stage_->segment_contact(before, after);
  } else if (after.y < tuning.debris_clearance && particle.velocity.y < 0) {
    hit = StageHit{{after.x, 0, after.z}, {0, 1, 0}, 0};
  }
  if (hit && hit->surface % 1000 != 8) {
    Vec3 normal = normalize(hit->normal);
    if (dot(normal, particle.velocity) > 0) {
      normal = normal * -1;
    }
    const float normal_speed = dot(normal, particle.velocity);
    const Vec3 slide = particle.velocity - normal * normal_speed;
    particle.velocity =
        slide * tuning.debris_friction - normal * (normal_speed * tuning.debris_restitution);
    after = hit->position + normal * tuning.debris_clearance;
    particle.spin *= tuning.debris_friction;
    if (normal.y > 0.5F && length(particle.velocity) <= tuning.debris_rest_speed) {
      particle.resting = true;
      particle.velocity = {};
      particle.spin = 0;
    }
  }
  particle.position = after;
}

} // namespace phantomcrash
