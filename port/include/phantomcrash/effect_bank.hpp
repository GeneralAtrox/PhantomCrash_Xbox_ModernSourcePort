#pragma once

#include "phantomcrash/fence_impacts.hpp"

#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// The particle effects (tables/effects.json, from effect_bank.py): the
// groups a caller starts, the scripts they run and the emitters' tuning.
// The original steps its effects once a battle frame, 30 times a second
// (0x255ab0); the times, rates and speeds here are the original's own, in
// frames or per second as effect_bank.py describes. Field names are
// researcher-assigned from the code that reads them.

// Keys of (time, value), read with straight lines between them.
struct EffectCurve {
  std::vector<std::array<float, 2>> keys;

  [[nodiscard]] bool empty() const noexcept {
    return keys.empty();
  }
  // The value at `time`: the first key's before it, the last key's after.
  [[nodiscard]] float at(float time) const;
};

// What a curve's time counts (0x064b8a).
enum class CurveTime : std::uint8_t { none, age, emitter_age, life_percent };

// Where a new particle starts (0x215b18, by shape type and variant).
enum class EffectShape : std::uint8_t {
  point,   // at the emitter (0x062a56)
  sphere,  // on a sphere around it (0x0638bd)
  cluster, // around the first particle (0x063aad)
};

// The script flags (the emitter's flags word, +0x10).
namespace effect_flags {
inline constexpr std::uint32_t replaceable = 0x4;       // may be taken over when all are busy
inline constexpr std::uint32_t inherit_velocity = 0x8;  // starts trailing the emitter's motion
inline constexpr std::uint32_t exact_spread = 0x10;     // leaves at the spread angle exactly
inline constexpr std::uint32_t radial_push = 0x20;      // spawn offset away from the direction
inline constexpr std::uint32_t random_direction = 0x40; // starts on a sphere of `push`
inline constexpr std::uint32_t spin = 0x80;             // turns while it flies
inline constexpr std::uint32_t streak = 0x100;          // drawn from it to where it goes
inline constexpr std::uint32_t streak_long = 0x200;     // ... a whole frame's motion
inline constexpr std::uint32_t streak_short = 0x400;    // ... a fifth of a frame's
inline constexpr std::uint32_t random_offset = 0x800;   // starts off the emitter
inline constexpr std::uint32_t fragments = 0x1000;      // a burst of copies
inline constexpr std::uint32_t detached = 0x2000;       // does not follow the emitter
inline constexpr std::uint32_t random_speed = 0x4000;   // speed times 0.2 to 1
inline constexpr std::uint32_t swirl = 0x8000;          // wind turning round
inline constexpr std::uint32_t die_at_target = 0x10000; // gone once it reaches the target
inline constexpr std::uint32_t debris = 0x20000;        // falls and lies on the ground
inline constexpr std::uint32_t debris_kind = 0x40000;   // drawn as debris
inline constexpr std::uint32_t kept = 0x80000;          // never taken over
inline constexpr std::uint32_t streak_line = 0x100000;  // drawn as a line
inline constexpr std::uint32_t streaks = streak | streak_long | streak_short | streak_line;
} // namespace effect_flags

// One emitter's tuning (0x150 bytes in the original).
struct EffectDefinition {
  // Force factors: the pull's is rand(-1, 1) * jitter + scale; the wind's
  // and the drag's are rand(-1, 1) * drag_jitter + drag.
  float force_scale{1.0F};
  float force_jitter{};
  float drag{};
  float drag_jitter{};
  bool additive{};     // drawn added to what is behind (+0x18)
  bool no_gravity{};   // (+0x1c)
  Vec3 gravity_angles; // radians, turning (0, -1, 0)
  float gravity{};
  bool wind_off{true};
  float wind{};
  Vec3 wind_angles; // degrees
  bool pull_off{true};
  float pull{};          // toward the target when negative
  bool pull_to_target{}; // or along pull_angles
  float pull_range{};
  Vec3 pull_angles; // degrees
  Vec3 target;
  CurveTime colour_time{CurveTime::age};
  CurveTime alpha_time{CurveTime::age};
  CurveTime mass_time{CurveTime::age};
  CurveTime scale_time{CurveTime::age};
  CurveTime min_speed_time{CurveTime::none};
  CurveTime max_speed_time{CurveTime::none};
  float life{1.0F}; // seconds
  float life_jitter{};
  float mass{1.0F};
  float size{1.0F};
  float size_jitter{};
  float min_speed{};       // per second
  float max_speed{1.0e5F}; // per second
  std::array<float, 3> colour{1.0F, 1.0F, 1.0F};
  float alpha{1.0F};
  float alpha_extra{};
  float colour_jitter{};
  EffectShape shape{EffectShape::point};
  bool sphere_filled{}; // shape type 3: anywhere inside the sphere
  int direction_mode{};
  Vec3 offset;
  Vec3 rotation;
  Vec3 scale{1.0F, 1.0F, 1.0F};
  float rate{}; // particles a second
  float rate_jitter{};
  float spread{}; // degrees
  float spread_jitter{};
  float speed{}; // per second
  float speed_jitter{};
  float push{};
  int cell{};
  int frames{}; // cells of animation, 15 a second
  EffectCurve blue, green, red, alpha_curve, scale_curve, pull_curve, wind_curve, rate_curve,
      mass_curve, speed_curve, min_speed_curve, max_speed_curve;
};

struct EffectEmitterSpec {
  std::uint32_t flags{};
  int definition{};
};

struct EffectScript {
  std::vector<EffectEmitterSpec> emitters;
};

struct EffectGroup {
  std::vector<int> scripts;
  std::vector<int> flashes; // EffectBank::flashes, started with the group (0x0659e5)
};

// The particles' texture: cell corners in texture coordinates.
struct EffectAtlas {
  std::string texture;
  std::array<float, 2> cell_size{0.125F, 0.25F};
  std::vector<std::array<float, 2>> cells;
};

// One mesh of a converted model (a model slot).
struct EffectPicture {
  std::string model;
  int mesh{-1};
};

// How a flash changes each frame (table 0x215ec8): fade holds and then
// fades over its last frames (0x0666be); rise climbs, jitters and fades
// (0x066770); pulse swells and flickers (0x0668ab); still does nothing.
enum class FlashUpdate { fade, rise, pulse, still };

// A flash (0x215e48): a flip-book of textures, one a frame, on a unit quad
// facing the camera where its group started, scaled by the group's scale;
// additive (0x0b0124) or alpha-blended (0x0affeb).
struct EffectFlash {
  std::vector<std::string> frames;
  int step{1};
  bool billboard{true};
  bool additive{true};
  FlashUpdate update{FlashUpdate::still};
};

// How a shot is drawn (0x2534a8, by weapon record +0x22; 0x09831e): its
// model scaled by `scale`, turned to its flight unless a billboard. With a
// trail, the exhaust model follows it `exhaust_offset` behind.
struct ShotLook {
  bool billboard{};
  std::optional<EffectPicture> model;
  std::string sprite;
  float scale{1.0F};
  bool trail{};
  float exhaust_offset{};
  Vec3 exhaust_scale{1.0F, 1.0F, 1.0F};
};

// Where a shot lands (0x253408, by weapon record +0x23; 0x065f28): the
// group it starts, the scale of the group's flashes, and whether it leaves
// a bullet hole.
struct ShotImpact {
  int group{-1};
  float flash_scale{1.0F};
  bool decal{};
};

// The bullet holes (0x0663d2): a ring of `count`, `scale` across.
struct EffectDecals {
  std::vector<std::string> textures;
  float scale{0.25F};
  int count{100};
  float depth_offset{0.01F}; // native separation from the receiving surface
  float scorch_scale{1.0F};  // fraction of the blast radius
  // A port choice: the scorch picture is a mid brown that, laid over a
  // dark stage at full brightness, reads lighter than the ground it burns.
  // Its colour is multiplied by this, so it darkens what it covers.
  std::array<float, 3> scorch_tint{0.2F, 0.2F, 0.2F};
  // Also the port's: share of a mark's half width that must lie on its
  // surface. Marks that overhang are halved, then dropped, so none hangs
  // off a fence or a ledge.
  float fit_reach{0.7F};
};

// 0x0702d8 / 0x0704a5: 33 positions, updated on alternate 30 Hz frames.
struct MissileTrailLook {
  std::string texture;
  int points{33};
  int capacity{50};
  float sample_hz{15.0F};
  float fade_seconds{1.0F};
  float head_width{0.1F};
  float tail_width{1.5F};
};

struct ParticleMotion {
  int fragment_copies{8}; // 0x060b7c: copies numbered 2 through 9
  float fragment_step{0.1F};
  float fragment_jitter{0.1F};
  float debris_restitution{};     // 0x062115: debris settles without bouncing
  float debris_friction{};        // optional native tangential retention, zero in the base data
  float debris_rest_speed{0.02F}; // per effect frame
  float debris_clearance{0.05F};
  // 0x062115 settles debris on flat ground at the clearance; true settles
  // it on the stage's own surfaces instead (a port option).
  bool debris_stage_contacts{};
  // 0x060107 starts an emitter at its definition's offset and 0x05fda8 then
  // moves it to where it was asked (0x064c4b sets only the position), so
  // its first frame's motion runs from the offset and births that inherit
  // the motion spread along it. False starts it still at the spawn point.
  bool start_at_offset{true};
};

// A held beam (0x09b9fe): the model's unit quad stretched from the muzzle
// to the tip, `width` across. It brightens over `fade_frames` held and dims
// over as many once let go.
struct BeamLook {
  std::optional<EffectPicture> model;
  float width{0.25F};
  float fade_frames{8.0F};
};

struct EffectBank {
  float frame_rate{30.0F};
  EffectAtlas atlas;
  std::vector<EffectGroup> groups;
  std::vector<EffectScript> scripts;
  std::vector<EffectDefinition> definitions;
  std::vector<EffectFlash> flashes;
  std::vector<ShotLook> shots;
  std::optional<EffectPicture> exhaust;
  BeamLook beam;
  std::vector<ShotImpact> impacts;
  EffectDecals decals;
  MissileTrailLook missile_trail;
  ParticleMotion particle_motion;
  FenceLook fence;
  std::vector<int> wreck_groups; // started where a robot is destroyed (0x07675e)
};

// Throws std::runtime_error naming what is malformed.
[[nodiscard]] EffectBank parse_effect_bank(const json::JsonValue &value);
// The flashes, shots, impacts and decals (effect_pictures.cpp).
void parse_effect_pictures(const json::JsonValue &value, EffectBank &bank);

} // namespace phantomcrash
