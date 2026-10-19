// The emitters: starting a group, moving it, and each frame's throw of new
// particles (0x057760, 0x05fda8, 0x060107, 0x060508, 0x06077e, 0x0609d8).
#include "phantomcrash/effect_system.hpp"

#include "phantomcrash/skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace phantomcrash {

namespace {

// +y along `direction`, for row vectors (the rows are the new axes).
Mat4 heading_along(const Vec3 direction) {
  const float size = length(direction);
  const Vec3 y = size > 0.0F ? direction * (1.0F / size) : Vec3{0.0F, 1.0F, 0.0F};
  const Vec3 helper = std::fabs(y.y) < 0.99F ? Vec3{0.0F, 1.0F, 0.0F} : Vec3{1.0F, 0.0F, 0.0F};
  const Vec3 x = normalize(cross(helper, y));
  const Vec3 z = cross(x, y);
  Mat4 m;
  m.at(0, 0) = x.x;
  m.at(0, 1) = x.y;
  m.at(0, 2) = x.z;
  m.at(1, 0) = y.x;
  m.at(1, 1) = y.y;
  m.at(1, 2) = y.z;
  m.at(2, 0) = z.x;
  m.at(2, 1) = z.y;
  m.at(2, 2) = z.z;
  return m;
}

Vec3 turned(const Vec3 v, const Mat4 &m) {
  return {v.x * m.at(0, 0) + v.y * m.at(1, 0) + v.z * m.at(2, 0),
          v.x * m.at(0, 1) + v.y * m.at(1, 1) + v.z * m.at(2, 1),
          v.x * m.at(0, 2) + v.y * m.at(1, 2) + v.z * m.at(2, 2)};
}

} // namespace

EffectSystem::EffectSystem(std::shared_ptr<const EffectBank> bank, const unsigned seed)
    : bank_(std::move(bank)), random_(seed) {
  emitters_.reserve(emitter_slots);
}

float EffectSystem::random(const float low, const float high) {
  const float unit = static_cast<float>(random_() - std::minstd_rand::min()) /
                     static_cast<float>(std::minstd_rand::max() - std::minstd_rand::min());
  return low + (high - low) * unit;
}

EffectEmitter *EffectSystem::free_emitter() {
  if (emitters_.size() < static_cast<std::size_t>(emitter_slots)) {
    return &emitters_.emplace_back();
  }
  // All busy: 0x05fda8 takes over one that allows it.
  for (EffectEmitter &emitter : emitters_) {
    if ((emitter.flags & effect_flags::replaceable) != 0 &&
        (emitter.flags & effect_flags::kept) == 0) {
      emitter = EffectEmitter{};
      return &emitter;
    }
  }
  return nullptr;
}

EffectHandle EffectSystem::start(const int group, const Vec3 position, const Vec3 target,
                                 const Vec3 direction, const float flash_scale) {
  if (group < 0 || group >= static_cast<int>(bank_->groups.size())) {
    return 0;
  }
  const int instance = next_instance_++;
  const Mat4 orientation = heading_along(direction);
  const float fps = bank_->frame_rate;
  bool any = start_flashes(group, position, flash_scale, instance);
  for (const int script : bank_->groups[static_cast<std::size_t>(group)].scripts) {
    for (const EffectEmitterSpec &spec :
         bank_->scripts[static_cast<std::size_t>(script)].emitters) {
      EffectEmitter *slot = free_emitter();
      if (slot == nullptr) {
        continue;
      }
      EffectEmitter &emitter = *slot;
      const EffectDefinition &d = bank_->definitions[static_cast<std::size_t>(spec.definition)];
      emitter.definition = spec.definition;
      emitter.flags = spec.flags;
      emitter.instance = instance;
      emitter.group = group;
      // The original starts at the definition's offset and is then moved,
      // so its first frame's motion runs from there (ParticleMotion).
      emitter.position = position;
      emitter.previous = bank_->particle_motion.start_at_offset ? d.offset : position;
      emitter.target = target;
      emitter.orientation = orientation;
      if ((spec.flags & effect_flags::random_offset) != 0) {
        // 0x06039a chooses one shared anchor, then 0x06123e scatters
        // individual particles around it. This is not the pull target.
        constexpr float minimum_reach = 0.2F;
        const float pitch = random(0.0F, pi);
        const float roll = random(-pi, pi);
        const float reach = d.push * random(minimum_reach, 1.0F);
        emitter.spawn_offset = turned({reach, 0, 0}, rotation_z(roll) * rotation_y(pitch));
      }
      emitter.rate = d.rate;
      if (!d.no_gravity) {
        const Mat4 turn = rotation_x(d.gravity_angles.x) * rotation_y(d.gravity_angles.y) *
                          rotation_z(d.gravity_angles.z);
        emitter.gravity = turned({0.0F, -1.0F, 0.0F}, turn) * (d.gravity / (fps * fps));
      }
      emitter.particles.assign(particle_slots, EffectParticle{});
      any = true;
    }
  }
  return any ? instance : 0;
}

void EffectSystem::move(const EffectHandle handle, const Vec3 position) {
  for (EffectEmitter &emitter : emitters_) {
    if (emitter.instance == handle) {
      emitter.position = position;
    }
  }
}

void EffectSystem::set_direction(const EffectHandle handle, const Vec3 direction) {
  const Mat4 orientation = heading_along(direction);
  for (EffectEmitter &emitter : emitters_) {
    if (emitter.instance == handle) {
      emitter.orientation = orientation;
    }
  }
}

void EffectSystem::set_segment(const EffectHandle handle, const Vec3 segment) {
  const bool spans = length(segment) > 0.0F;
  for (EffectEmitter &emitter : emitters_) {
    if (emitter.instance == handle) {
      emitter.segment = segment;
      if (spans) {
        emitter.orientation = heading_along(segment);
      }
    }
  }
}

void EffectSystem::set_target(const EffectHandle handle, const Vec3 target) {
  for (EffectEmitter &emitter : emitters_) {
    if (emitter.instance == handle) {
      emitter.target = target;
    }
  }
}

void EffectSystem::set_rate(const EffectHandle handle, const float per_second) {
  for (EffectEmitter &emitter : emitters_) {
    if (emitter.instance == handle) {
      emitter.rate = per_second;
    }
  }
}

void EffectSystem::stop(const EffectHandle handle, const bool include_pending) {
  for (EffectEmitter &emitter : emitters_) {
    if (handle != 0 && emitter.instance == handle && (emitter.spawned || include_pending)) {
      emitter.spawned = true;
      emitter.stopped = true;
      emitter.emitting = false;
    }
  }
}

bool EffectSystem::alive(const EffectHandle handle) const {
  const auto started = [&](const auto &item) { return item.instance == handle; };
  return handle != 0 && (std::any_of(emitters_.begin(), emitters_.end(), started) ||
                         std::any_of(flashes_.begin(), flashes_.end(), started));
}

void EffectSystem::clear() {
  emitters_.clear();
  flashes_.clear();
  owed_time_ = 0.0F;
}

void EffectSystem::tick(const float seconds) {
  const float frame = 1.0F / bank_->frame_rate;
  owed_time_ += std::max(seconds, 0.0F);
  // A long stall does not replay every frame it missed.
  constexpr int most_frames = 8;
  int frames = 0;
  while (owed_time_ >= frame && frames < most_frames) {
    owed_time_ -= frame;
    step();
    ++frames;
  }
  owed_time_ = std::min(owed_time_, frame);
}

void EffectSystem::step() {
  for (EffectEmitter &emitter : emitters_) {
    step(emitter);
  }
  std::erase_if(emitters_, [](const EffectEmitter &emitter) {
    return !emitter.emitting && emitter.spawned && emitter.live < 1;
  });
  step_flashes();
}

// 0x060508.
void EffectSystem::step(EffectEmitter &emitter) {
  emitter.velocity = emitter.position - emitter.previous;
  emitter.previous = emitter.position;
  for (EffectParticle &particle : emitter.particles) {
    if (particle.alive) {
      particle.previous = particle.position;
      update(emitter, particle);
    }
  }
  throw_particles(emitter);
  ++emitter.frame;
}

// 0x0609d8: this frame's share of the rate.
float EffectSystem::frame_rate_share(EffectEmitter &emitter) {
  if (emitter.stopped) {
    emitter.emitting = false;
    return 0.0F;
  }
  const EffectDefinition &d = bank_->definitions[static_cast<std::size_t>(emitter.definition)];
  float rate = 0.0F;
  if (d.rate_curve.empty()) {
    rate = emitter.rate + random(-1.0F, 1.0F) * d.rate_jitter;
  } else {
    const auto time = static_cast<float>(emitter.frame);
    rate = d.rate_curve.at(time);
    const auto &last = d.rate_curve.keys.back();
    if (time > last[0] && last[1] <= 0.0F) {
      emitter.emitting = false;
    }
  }
  return std::max(rate, 0.0F) / bank_->frame_rate;
}

// 0x06077e.
void EffectSystem::throw_particles(EffectEmitter &emitter) {
  emitter.owed += frame_rate_share(emitter);
  if (emitter.owed < 1.0F) {
    return;
  }
  const int due = static_cast<int>(emitter.owed);
  for (int i = 0; i < due; ++i) {
    if (emitter.live > particle_limit) {
      emitter.owed = 0.0F;
      return;
    }
    int slot = emitter.next_slot;
    for (int tries = 0; tries < particle_slots && emitter.particles[slot].alive; ++tries) {
      slot = (slot + 1) % particle_slots;
    }
    if (emitter.particles[slot].alive) {
      emitter.owed = 0.0F;
      return;
    }
    emitter.next_slot = (slot + 1) % particle_slots;
    ++emitter.live;
    spawn(emitter, emitter.particles[slot]);
    emitter.owed -= 1.0F;
    emitter.spawned = true;
  }
}

} // namespace phantomcrash
