#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"
#include "phantomcrash/source_effect_slots.hpp"
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace phantomcrash {
struct SourceGenerator {
  unsigned flags{};
  int capacity{}, shape{}, blend{}, size_curve{}, colour_curve{}, alpha_curve{}, gravity_curve{};
  float rate{}, start{}, stop{}, speed{}, speed_spread{}, cone{}, cone_spread{};
  float lifetime{}, lifetime_spread{}, gravity{}, gravity_spread{}, gravity_end{}, drag{1};
  float size_start{}, size_end{}, rotation_range{}, spin{}, spin_spread{}, fade_delay{};
  float atlas_period{};
  int atlas_frames{1}, atlas_curve{};
  Vec3 dimensions, offset, rotation, render_size;
  std::array<float, 4> colour_a{}, colour_b{};
  std::string texture;
};
struct SourceParticleDefinition {
  std::vector<SourceGenerator> generators;
};
struct SourceParticleSpawn {
  Vec3 position, velocity;
  std::array<Vec3, 2> quad_axes;
};
// Standalone PTK births in unscaled native coordinates. Parent frames are
// applied separately by the source; this samples only the generator's fields.
[[nodiscard]] SourceParticleSpawn sample_source_particle_spawn(const SourceGenerator &generator,
                                                               std::mt19937 &random);
[[nodiscard]] SourceParticleDefinition
parse_source_particles(const json::JsonValue &value, const json::JsonValue *atlas = nullptr);
[[nodiscard]] float source_particle_curve(int code, float from, float to, float progress);

struct SourceParticle {
  Vec3 previous, position, velocity;
  std::array<Vec3, 2> quad_axes;
  Vec3 local_position;
  std::array<Vec3, 2> local_quad_axes;
  Mat4 birth_frame;
  bool inertia_enabled{};
  float inertia_weight{}, inertia_decay{};
  float age{}, lifetime{}, rotation{}, spin{}, gravity{}, scale{};
  int atlas_frame{-1};
  float atlas_clock{};
};
[[nodiscard]] float source_particle_angle(float radians);
[[nodiscard]] std::array<float, 4> source_particle_colour(const SourceGenerator &generator,
                                                          const SourceParticle &particle);
[[nodiscard]] std::array<float, 4> source_particle_uv(const SourceGenerator &generator,
                                                      const SourceParticle &particle);
// Effective parent transport in native coordinates. Movement is a native
// displacement per source tick, not a velocity per second. The caller supplies
// the source model/action frame after its attachment mask and Euler composition.
struct SourceParticleParent {
  Mat4 frame;
  unsigned flags{};
  float scale{1};
  Vec3 movement;
  int vertical_gate{};
  unsigned movement_flags{};
  float inertia_weight{}, inertia_decay{};
};
struct SourceEmitter {
  const SourceGenerator *generator{};
  SourceParticleParent parent;
  float scale{.1F}, age{}, accumulator{};
  bool stopped{};
  std::uint64_t key{};
  std::uint64_t effect_handle{};
  std::vector<SourceParticle> particles;
};

// PTK births and lifetime curves in source ticks. The host chooses the clock
// cadence in data; presentation interpolation does not advance simulation.
class SourceParticles final {
public:
  SourceParticles(float update_hz, float world_scale);
  void start(const SourceParticleDefinition &definition, Vec3 at, std::uint64_t key = 0);
  void start_parented(const SourceParticleDefinition &definition,
                      const SourceParticleParent &parent, std::uint64_t key = 0);
  [[nodiscard]] bool start_managed(const SourceParticleDefinition &definition,
                                   const SourceParticleParent &parent, std::uint64_t key,
                                   int resource,
                                   std::optional<SourceEffectPriority> priority = std::nullopt,
                                   int owner = -1);
  [[nodiscard]] std::size_t managed_effects() const {
    return slots_.size();
  }
  [[nodiscard]] std::size_t managed_count(int resource, int owner) const {
    return slots_.count(resource, owner);
  }
  void set_parent(std::uint64_t key, const SourceParticleParent &parent);
  void stop(std::uint64_t key);
  void move(std::uint64_t key, Vec3 at);
  [[nodiscard]] bool contains(std::uint64_t key) const;
  void tick(float seconds);
  [[nodiscard]] const std::vector<SourceEmitter> &emitters() const {
    return emitters_;
  }
  [[nodiscard]] float between() const {
    return accumulator_ * update_hz_;
  }
  [[nodiscard]] float world_scale() const {
    return world_scale_;
  }

private:
  float random(float center, float spread);
  SourceParticle birth(const SourceEmitter &emitter);
  void frame(SourceParticle &particle, const SourceParticleParent &parent, bool advance);
  void step();
  float update_hz_, world_scale_, accumulator_{};
  std::mt19937 random_{std::random_device{}()};
  std::vector<SourceEmitter> emitters_;
  SourceEffectSlots slots_;
  std::uint64_t next_handle_{};
};
} // namespace phantomcrash
