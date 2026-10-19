#pragma once

#include "phantomcrash/effect_bank.hpp"
#include "phantomcrash/math3d.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

namespace phantomcrash {
class StageCollision;

// The particle effects in motion, after the original's effect system
// (0x060508 and what it calls): a started group puts one emitter in play
// for each emitter of its scripts, and each emitter throws particles that
// fly, change colour and size along their curves and die.
//
// It steps in the original's frames (EffectBank::frame_rate a second), so
// the tuning keeps its meaning; drawing blends each particle between its
// last two frames.

struct EffectParticle {
  Vec3 position;
  Vec3 previous; // at the last frame, for drawing between frames
  Vec3 velocity; // per frame
  Vec3 gravity;  // per frame, squared
  int age{};     // frames
  int life{};
  float scale{1.0F}; // the scale curve's (+0xd0)
  float size{1.0F};  // the definition's size, when there is no scale curve (+0xdc)
  float rotation{};
  float spin{};
  int cell{};
  int kind{}; // 0 plain, 1 and 2 a cluster's centre and parts, 9 debris
  std::array<std::uint8_t, 4> colour{255, 255, 255, 255}; // r, g, b, a
  bool alive{};
  bool resting{};
  float swirl_yaw{};
  float swirl_weight{};

  [[nodiscard]] float sprite_half_extent() const noexcept {
    // 0x0625e4 submits half the size; particle.xvu expands the quad from
    // 0x280d90 with corners at +/-0.5. WorldSprite instead takes a radius.
    constexpr float submitted_size_scale = 0.5F;
    constexpr float quad_half_extent = 0.5F;
    constexpr float cluster_centre_scale = 3.5F; // 0x0bf4cf, constant 0x19f918
    const float extent = scale * size * submitted_size_scale * quad_half_extent;
    return kind == 1 ? extent * cluster_centre_scale : extent;
  }
};

// One emitter: up to `particle_slots` particles.
struct EffectEmitter {
  int definition{};
  std::uint32_t flags{};
  int instance{}; // the start() it belongs to
  int group{};    // ... and the group that start() played
  Vec3 position;
  Vec3 previous;
  Vec3 velocity; // per frame
  Vec3 target;
  Vec3 spawn_offset; // random_offset's per-emitter anchor (0x060107, +0x74)
  Mat4 orientation;  // turns +y to the effect's direction
  Vec3 segment;      // an arc's span (0x065058 frame row 1); zero: the fragment step
  Vec3 gravity;      // per frame, squared
  float rate{};      // particles a second, when there is no rate curve
  float owed{};      // particles due and not yet thrown
  int frame{};
  bool emitting{true};
  bool spawned{};
  bool stopped{};
  int live{};
  int next_slot{};
  std::vector<EffectParticle> particles;
};

// A flash in play (0x96659c, 0x74 bytes each): which EffectBank flash, how
// many frames it has shown, and what its update last set.
struct EffectFlashState {
  int flash{};
  int instance{};
  int group{};
  int age{}; // frames; the picture shown is frames[age]
  Vec3 position;
  Vec3 previous;
  float scale{1.0F}; // +0x70, the group's flash scale
  float grow{1.0F};  // the rise's growth, kept in its matrix
  std::uint8_t alpha{255};
};

// A started group; 0 is none.
using EffectHandle = int;

class EffectSystem final {
public:
  static constexpr int emitter_slots = 0x90;     // 0x05fda8
  static constexpr int particle_slots = 0x70;    // per emitter
  static constexpr int particle_limit = 0x6e;    // live ones before spawning stops (0x06077e)
  static constexpr float animation_rate = 15.0F; // atlas cells a second (0x061d35)
  static constexpr int flash_slots = 0x80;       // 0x96659c

  explicit EffectSystem(std::shared_ptr<const EffectBank> bank, unsigned seed = 0x5eed);

  [[nodiscard]] const EffectBank &bank() const noexcept {
    return *bank_;
  }
  // Starts group `group` at `position`, heading along `direction` (its +y),
  // with `target` for emitters that pull toward it or stop there, and its
  // flashes at `flash_scale`. 0 when the group is unknown or starts nothing.
  EffectHandle start(int group, Vec3 position, Vec3 target = {}, Vec3 direction = {0, 1, 0},
                     float flash_scale = 1.0F);
  // Moves a started group's emitters; their motion carries the particles
  // that follow them.
  void move(EffectHandle handle, Vec3 position);
  void set_target(EffectHandle handle, Vec3 target);
  void set_direction(EffectHandle handle, Vec3 direction);
  // Spans a started group's fragment chains along `segment` from where it
  // is, as the electric arcs' frames do (0x087063); zero restores the step.
  void set_segment(EffectHandle handle, Vec3 segment);
  // Particles a second for the group's emitters (0x087eac sets a pickup's).
  void set_rate(EffectHandle handle, float per_second);
  // No more particles; those in flight live out their lives (0x06516c).
  // include_pending also cancels an attached emitter before its first particle.
  void stop(EffectHandle handle, bool include_pending = false);
  [[nodiscard]] bool alive(EffectHandle handle) const;
  // Ends every effect.
  void clear();
  void set_stage(const StageCollision *stage) {
    stage_ = stage;
  }

  // Advances by `seconds`, whole frames at a time.
  void tick(float seconds);
  // How far the time is past the last frame, 0..1, to draw between frames.
  [[nodiscard]] float between() const noexcept {
    return owed_time_ * bank_->frame_rate;
  }
  [[nodiscard]] const std::vector<EffectEmitter> &emitters() const noexcept {
    return emitters_;
  }
  [[nodiscard]] const std::vector<EffectFlashState> &flashes() const noexcept {
    return flashes_;
  }

  // One frame of every emitter (public for the tests).
  void step();

private:
  void step(EffectEmitter &emitter);
  void throw_particles(EffectEmitter &emitter);
  [[nodiscard]] float frame_rate_share(EffectEmitter &emitter);
  void spawn(EffectEmitter &emitter, EffectParticle &particle);
  void spawn_fragments(EffectEmitter &emitter, EffectParticle &seed);
  void move_debris(EffectEmitter &emitter, EffectParticle &particle);
  void update(EffectEmitter &emitter, EffectParticle &particle);
  void move_particle(EffectEmitter &emitter, EffectParticle &particle);
  void accelerate(const EffectEmitter &emitter, EffectParticle &particle);
  void colour_particle(const EffectEmitter &emitter, EffectParticle &particle);
  void kill(EffectEmitter &emitter, EffectParticle &particle);
  [[nodiscard]] float curve_time(const EffectEmitter &emitter, const EffectParticle &particle,
                                 CurveTime mode) const;
  [[nodiscard]] Vec3 cone(Vec3 direction, float spread, float speed, std::uint32_t flags);
  [[nodiscard]] float random(float low, float high);
  [[nodiscard]] EffectEmitter *free_emitter();
  // effect_flashes.cpp.
  bool start_flashes(int group, Vec3 position, float scale, int instance);
  void step_flashes();
  void update(EffectFlashState &flash);

  std::shared_ptr<const EffectBank> bank_;
  const StageCollision *stage_{}; // battle collision outlives this effect system
  std::vector<EffectEmitter> emitters_;
  std::vector<EffectFlashState> flashes_;
  float pulse_phase_{}; // 0x9665ac, shared by every pulsing flash
  std::minstd_rand random_;
  float owed_time_{};
  int next_instance_{1};
};

} // namespace phantomcrash
