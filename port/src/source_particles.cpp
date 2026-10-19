#include "phantomcrash/source_particles.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/skeleton.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace phantomcrash {
namespace {
Vec3 vector(const json::JsonValue &v) {
  const auto &a = v.array();
  return {static_cast<float>(a.at(0).number()), static_cast<float>(a.at(1).number()),
          static_cast<float>(a.at(2).number())};
}
float field(const json::JsonValue &v, const char *key) {
  return static_cast<float>(json::required(v, key).number());
}
} // namespace

SourceParticleDefinition parse_source_particles(const json::JsonValue &value,
                                                const json::JsonValue *atlas) {
  SourceParticleDefinition result;
  for (const auto &record : json::required(value, "generators").array()) {
    const auto &v = json::required(record, "decoded_fields");
    SourceGenerator g;
    g.flags = static_cast<unsigned>(json::required(v, "flags").number());
    g.capacity = static_cast<int>(field(v, "maximum_live_particles"));
    g.rate = field(v, "emission_rate_source_units");
    if (g.capacity <= 0 || g.rate <= 0) {
      continue;
    }
    g.start = field(v, "emission_start_source_time");
    g.stop = field(v, "emission_stop_source_time");
    g.shape = static_cast<int>(field(v, "spawn_shape_code"));
    g.blend = static_cast<int>(field(v, "blend_code"));
    g.dimensions = vector(json::required(v, "spawn_dimensions"));
    g.offset = vector(json::required(v, "spawn_offset"));
    g.rotation = vector(json::required(v, "rotation_degrees"));
    g.render_size = vector(json::required(v, "render_size"));
    const auto spread = [&](const char *key, float &center, float &radius) {
      const auto &s = json::required(v, key);
      center = field(s, "center");
      radius = field(s, "spread");
    };
    spread("initial_speed", g.speed, g.speed_spread);
    spread("direction_angle_degrees", g.cone, g.cone_spread);
    spread("lifetime_source_units", g.lifetime, g.lifetime_spread);
    spread("gravity", g.gravity, g.gravity_spread);
    spread("spin_degrees", g.spin, g.spin_spread);
    g.gravity_end = field(v, "gravity_end");
    g.gravity_curve = static_cast<int>(field(v, "gravity_curve"));
    g.drag = field(v, "velocity_multiplier_per_tick");
    g.fade_delay = field(v, "fade_delay_source_time");
    g.size_start = field(v, "size_start");
    g.size_end = field(v, "size_end");
    g.size_curve = static_cast<int>(field(v, "size_curve"));
    g.colour_curve = static_cast<int>(field(v, "colour_curve"));
    g.alpha_curve = static_cast<int>(field(v, "alpha_curve"));
    g.atlas_period = static_cast<float>(v.number_or("atlas_frame_period_source_time", 0));
    g.atlas_curve = static_cast<int>(v.number_or("atlas_lifetime_curve", 0));
    // 0x19e570 wraps the range once; live angles also normalize negative remainders.
    g.rotation_range = std::fmod(field(v, "rotation_range_radians") + pi, 2 * pi) - pi;
    for (int i = 0; i < 4; ++i) {
      g.colour_a[i] = static_cast<float>(json::required(v, "colour_a_bytes").array()[i].number());
      g.colour_b[i] = static_cast<float>(json::required(v, "colour_b_bytes").array()[i].number());
    }
    g.texture = v.string_or("texture_path", "");
    if (atlas) {
      g.atlas_frames = std::max(1, static_cast<int>(atlas->number_or(g.texture, 1)));
    }
    result.generators.push_back(std::move(g));
  }
  return result;
}

float source_particle_curve(const int code, const float from, const float to, float t) {
  t = std::clamp(t, 0.0F, 1.0F);
  switch (code) {
  case 0:
    return from;
  case 2:
    t *= t;
    break;
  case 3:
    t = 1 - t * t * t * t;
    break;
  case 4:
    t = (1 + std::cos(2 * pi * t)) * .5F;
    break;
  case 5:
    t = std::cos(pi * t);
    break;
  case 6:
    t = 1 + std::cos(pi + pi * t);
    break;
  case 7:
    t = (1 + std::sin(2 * pi * t)) * .5F;
    break;
  case 8:
    t = (1 + std::sin(pi * t)) * .5F;
    break;
  case 9:
    t = (1 + std::sin(pi + pi * t)) * .5F;
    break;
  default:
    break;
  }
  return from + (to - from) * t;
}

std::array<float, 4> source_particle_colour(const SourceGenerator &g, const SourceParticle &p) {
  std::array<float, 4> colour{1, 1, 1, 1};
  const float t = p.age / p.lifetime;
  // 0x19d230/268/2a0 halves textured RGB before GS modulation (128 is unity).
  const float rgb_range = (g.flags & 0x20000) ? 256.0F : 255.0F;
  for (int i = 0; i < 3; ++i) {
    if (g.flags & 0x800000) {
      colour[i] =
          source_particle_curve(g.colour_curve, g.colour_a[i], g.colour_b[i], t) / rgb_range;
    }
  }
  if (g.flags & 0x400000) {
    float alpha_t = t;
    if (g.fade_delay > 0 && p.lifetime > g.fade_delay) {
      alpha_t = std::max(0.0F, (p.age - g.fade_delay) / (p.lifetime - g.fade_delay));
    }
    // 0x199cd8 halves the authored alpha; 0x19d2d8 submits it unchanged.
    colour[3] = std::clamp(
        source_particle_curve(g.alpha_curve, g.colour_a[3], g.colour_b[3], alpha_t) / 256, 0.0F,
        1.0F);
  }
  return colour;
}
std::array<float, 4> source_particle_uv(const SourceGenerator &g, const SourceParticle &p) {
  const float count = static_cast<float>(g.atlas_frames);
  const int cell = (p.atlas_frame % g.atlas_frames + g.atlas_frames) % g.atlas_frames;
  // Keep source strip wrapping inside the image, including the -1 birth cell.
  return {cell / count, 0, (cell + 1) / count, 1};
}

SourceParticles::SourceParticles(const float hz, const float scale)
    : update_hz_(hz), world_scale_(scale) {
  if (!(hz > 0) || !(scale > 0)) {
    throw std::runtime_error("Invalid PTK host clock or scale");
  }
}
float source_particle_angle(float radians) {
  // 0x199ec8..f0c wraps the live angle into [-pi, pi].
  float wrapped = std::fmod(radians + pi, 2 * pi);
  if (wrapped < 0) {
    wrapped += 2 * pi;
  }
  return wrapped - pi;
}
float SourceParticles::random(const float center, const float spread) {
  return std::uniform_real_distribution<float>(center - std::abs(spread),
                                               center + std::abs(spread))(random_);
}
SourceParticle SourceParticles::birth(const SourceEmitter &e) {
  const auto &g = *e.generator;
  const auto spawn = sample_source_particle_spawn(g, random_);
  SourceParticle p;
  p.local_position = spawn.position * e.scale;
  p.velocity = spawn.velocity * e.scale;
  p.local_quad_axes = spawn.quad_axes;
  p.birth_frame = e.parent.frame;
  p.inertia_enabled = (e.parent.flags & 0x200) != 0;
  p.inertia_weight = e.parent.inertia_weight;
  p.inertia_decay = e.parent.inertia_decay;
  frame(p, e.parent, false);
  p.previous = p.position;
  p.lifetime = std::max(random(g.lifetime, g.lifetime_spread), 1.0F);
  p.gravity = random(g.gravity, g.gravity_spread);
  p.rotation = (g.flags & 0x100000) ? random(0, g.rotation_range) : 0;
  p.spin = (g.flags & 0x200000) ? g.spin * pi / 180 : 0;
  p.scale = e.scale;
  return p;
}
void SourceParticles::start(const SourceParticleDefinition &definition, const Vec3 at,
                            const std::uint64_t key) {
  SourceParticleParent parent;
  parent.frame = translation(at);
  start_parented(definition, parent, key);
}
void SourceParticles::start_parented(const SourceParticleDefinition &definition,
                                     const SourceParticleParent &parent, const std::uint64_t key) {
  bool found = false;
  if (key) {
    for (auto &e : emitters_) {
      if (e.key == key && !e.stopped) {
        e.parent = parent;
        e.scale = world_scale_ * parent.scale;
        found = true;
      }
    }
  }
  if (found) {
    return;
  }
  for (const auto &g : definition.generators) {
    if (!(g.flags & 1)) {
      continue; // 0x19c1dc..1e4 skips the whole generator update
    }
    SourceEmitter e;
    e.generator = &g;
    e.parent = parent;
    e.scale = world_scale_ * parent.scale;
    e.key = key;
    emitters_.push_back(std::move(e));
  }
}
void SourceParticles::set_parent(const std::uint64_t key, const SourceParticleParent &parent) {
  for (auto &e : emitters_) {
    if (e.key == key) {
      e.parent = parent;
      e.scale = world_scale_ * parent.scale;
    }
  }
}
bool SourceParticles::start_managed(const SourceParticleDefinition &definition,
                                    const SourceParticleParent &parent, const std::uint64_t key,
                                    const int resource,
                                    const std::optional<SourceEffectPriority> priority,
                                    const int owner) {
  if (key &&
      std::ranges::any_of(emitters_, [key](const auto &e) { return e.key == key && !e.stopped; })) {
    start_parented(definition, parent, key);
    return true;
  }
  const auto handle = ++next_handle_;
  const auto allocation = slots_.allocate(handle, resource, priority, owner);
  if (!allocation) {
    return false;
  }
  if (allocation->evicted) {
    std::erase_if(emitters_, [&](const auto &e) { return e.effect_handle == allocation->evicted; });
  }
  const auto first = emitters_.size();
  start_parented(definition, parent, key);
  for (std::size_t i = first; i < emitters_.size(); ++i) {
    emitters_[i].effect_handle = handle;
  }
  if (first == emitters_.size()) {
    slots_.release(handle);
  }
  return true;
}
void SourceParticles::stop(const std::uint64_t key) {
  for (auto &e : emitters_) {
    if (e.key == key) {
      e.stopped = true;
    }
  }
}
void SourceParticles::move(const std::uint64_t key, const Vec3 at) {
  for (auto &e : emitters_) {
    if (e.key == key) {
      e.parent.frame.at(3, 0) = at.x;
      e.parent.frame.at(3, 1) = at.y;
      e.parent.frame.at(3, 2) = at.z;
    }
  }
}
bool SourceParticles::contains(const std::uint64_t key) const {
  return std::ranges::any_of(emitters_, [key](const auto &e) { return e.key == key; });
}
void SourceParticles::frame(SourceParticle &p, const SourceParticleParent &parent,
                            const bool advance) {
  // 0x19c3e4/480 chooses each particle's frozen frame for flags 0x300.
  Mat4 current = parent.frame;
  Mat4 &world = (parent.flags & 0x300) ? p.birth_frame : current;
  if (advance && p.inertia_enabled) {
    Vec3 movement = parent.movement * p.inertia_weight;
    if (parent.vertical_gate == 1) {
      movement.y = 0;
    }
    if (parent.movement_flags & 0x10) {
      movement = {};
    }
    movement = movement * .8F; // 0x19a0b8..dc; applied before world composition
    world.at(3, 0) += movement.x;
    world.at(3, 1) += movement.y;
    world.at(3, 2) += movement.z;
    p.inertia_weight -= p.inertia_decay; // no clamp, including suppressed updates
  }
  p.position = transform_point(p.local_position, world);
  Mat4 basis = world;
  basis.at(3, 0) = basis.at(3, 1) = basis.at(3, 2) = 0;
  for (std::size_t axis = 0; axis < p.quad_axes.size(); ++axis) {
    p.quad_axes[axis] = transform_point(p.local_quad_axes[axis], basis);
  }
}
void SourceParticles::step() {
  slots_.step();
  for (auto &e : emitters_) {
    const auto &g = *e.generator;
    for (auto &p : e.particles) {
      p.previous = p.position;
      ++p.age;
      const float t = p.age / p.lifetime;
      p.velocity.y -= source_particle_curve(g.gravity_curve, p.gravity, g.gravity_end, t) * p.scale;
      p.velocity = p.velocity * g.drag;
      p.local_position = p.local_position + p.velocity;
      frame(p, e.parent, true);
      if (g.flags & 0x200000) {
        p.rotation = source_particle_angle(p.rotation + random(p.spin, g.spin_spread * pi / 180));
      }
      if ((g.flags & 0x40000) && g.atlas_period > 0) {
        // 0x199d70..e34 decrements by twice the source timestep and advances
        // until the authored frame timer is positive, even across multiple cells.
        p.atlas_clock -= 2;
        do {
          if (p.atlas_clock > 0) {
            break;
          }
          p.atlas_clock += g.atlas_period;
          if (g.flags & 0x80000) {
            p.atlas_frame = static_cast<int>(
                std::lround(random((g.atlas_frames - 1) * .5F, (g.atlas_frames - 1) * .5F)));
          } else {
            p.atlas_frame = (p.atlas_frame + 1) % g.atlas_frames;
          }
        } while (p.atlas_clock <= 0);
      } else {
        p.atlas_frame =
            static_cast<int>(g.atlas_frames * source_particle_curve(g.atlas_curve, 0, .99F, t));
      }
    }
    std::erase_if(e.particles, [](const auto &p) { return p.age > p.lifetime; });
    ++e.age;
    if ((g.flags & 4) && e.age > g.stop) {
      e.stopped = true;
    }
    if (e.stopped || ((g.flags & 2) && e.age < g.start)) {
      continue;
    }
    e.accumulator += g.rate;
    while (e.accumulator >= 1 && e.particles.size() < static_cast<std::size_t>(g.capacity)) {
      e.particles.push_back(birth(e));
      e.accumulator -= 1;
    }
    if (e.particles.size() >= static_cast<std::size_t>(g.capacity)) {
      e.accumulator = 0;
    }
  }
  std::vector<std::uint64_t> retired;
  std::erase_if(emitters_, [&](const auto &e) {
    if (e.stopped && e.particles.empty()) {
      if (e.effect_handle) {
        retired.push_back(e.effect_handle);
      }
      return true;
    }
    return false;
  });
  for (const auto handle : retired) {
    if (!std::ranges::any_of(emitters_,
                             [handle](const auto &e) { return e.effect_handle == handle; })) {
      slots_.release(handle);
    }
  }
}
void SourceParticles::tick(const float seconds) {
  accumulator_ += std::max(seconds, 0.0F);
  const float step_seconds = 1 / update_hz_;
  while (accumulator_ >= step_seconds) {
    step();
    accumulator_ -= step_seconds;
  }
}
} // namespace phantomcrash
