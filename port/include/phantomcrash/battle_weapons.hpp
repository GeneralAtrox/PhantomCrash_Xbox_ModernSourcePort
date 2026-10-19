#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/stage_collision.hpp"
#include "phantomcrash/stage_doors.hpp"

#include <map>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

// How a weapon behaves in the port's battle, tuned in
// data/world/training.json "weapons". A weapon naming an original record
// ("part", robot_parts.hpp) takes its damage, timing, flight, blast,
// homing, ammunition and HUD kind from that record instead.
enum class WeaponKind { gun, melee, missile, grenade };

// The HUD's weapon kinds (weapon record +0x1c, drawn by 0x4ad3b).
namespace hud_kind {
inline constexpr int cannon = 0; // pistols and cannons
inline constexpr int missile = 1;
inline constexpr int machine_gun = 2; // machine guns and shotguns
inline constexpr int grenade = 3;
inline constexpr int beam = 5; // charged: beams and lasers
inline constexpr int melee = 6;
} // namespace hud_kind

// What a shot does where it lands (weapon record +0x1f and +0x20, the
// cases of 0x098eda).
namespace shot_hit {
inline constexpr int removed = 0;       // gone, without an impact
inline constexpr int burst = 1;         // gone, with its impact
inline constexpr int explode = 2;       // blasts, with its impact
inline constexpr int explode_quiet = 3; // blasts without one
inline constexpr int strike = 4;        // its impact and a hit sound (melee)
inline constexpr int pierce = 5;        // its impact, and it flies on (beams)
} // namespace shot_hit

struct WeaponTuning {
  std::string name;
  std::string label; // shown on the HUD (the name when not given)
  WeaponKind kind{WeaponKind::gun};
  float damage{30.0F};             // a direct hit's
  float cooldown{0.15F};           // seconds between shots
  int rounds{1};                   // shots a trigger pull fires
  float spread_degrees{};          // the extra rounds scatter this far off the aim
  float speed{180.0F};             // projectile (top) speed, units per second
  float start_speed{};             // rounds leave this fast (0: the speed; missiles half) ...
  float acceleration{};            // ... and speed up by this a second (missiles 0: the speed)
  float range{150.0F};             // the farthest it is fired at; melee reach, beam length
  float min_range{};               // the nearest the computer fires it at (lobbed rounds)
  float life_seconds{};            // projectiles expire after this (0: past the range)
  float lock_range{120.0F};        // the aim locks onto enemies this close, in its box
  float hold_degrees{};            // a lock made holds this far off the aim (0: no hold)
  float turn_degrees_per_second{}; // missiles home at this rate
  float gravity{};                 // grenades fall
  float launch_degrees{};          // grenades leave this far above the aim (0: lobbed onto it)
  float blast_radius{};            // missiles and grenades hurt everyone this close
  float blast_start_radius{};      // the blast grows from this ...
  float blast_seconds{};           // ... to blast_radius over this long
  float blast_damage{};            // to each body in it every tick (0: damage, less with distance)
  float stagger_seconds{0.2F};     // a hit makes its target flinch this long (0: never)
  float knockback{};               // a hit pushes its target this fast, units a second
  float knockdown_percent{};       // a critical hit's chance to knock off a weapon
  float recharge_seconds{};        // a beam recharges from empty to full in this long
  std::optional<PartRef> part;     // the original record it is
  int hud_kind{hud_kind::cannon};
  int source_melee_kind{-1};
  int model{-1};      // its record's model (+8), which some fire sounds go by
  int fire_sound{-1}; // its record's fire sound (+0x21, battle_sounds.hpp); -1: none
  int flight{-1};     // its record's flight (+0x1d)
  int aim_mode{-1};   // weapon-specific shoulder targeting (+0x1e)
  // How its record draws it (effects.json): the shots' look (+0x22), the
  // impact they start where they land (+0x23), and what they do there
  // (+0x1f on a wall or the floor, +0x20 on a robot; see shot_hit). -1 and
  // 0: none.
  int look{-1};
  int impact{-1};
  int surface_hit{};
  int robot_hit{};
  int max_ammo{}; // 0: unlimited
  bool homing{};  // missiles home on their lock
};

// A weapon's ammunition, and the count the HUD shows. A refill counts the
// shown value up to the new amount over refill_count_seconds (0x09b986
// counts weapon +0x98 from +0x9c to +0x94 over the 120 frames at +0xa4).
// A beam's charge instead recharges by max / recharge_seconds a second
// (0x097785 adds max / recharge frames every battle tick), only while the
// beam is not held or fading (0x09ba08 calls it once the fade is out).
struct Magazine {
  static constexpr float refill_count_seconds = 2.0F;

  int max{}; // 0: unlimited
  int ammo{};
  float shown{};
  float refill_from{};
  float refill_left{};      // seconds of the count still to run
  float dry_seconds{};      // how long it has been empty
  float recharge_seconds{}; // 0: it does not recharge
  float charge{};           // the part of the next round recharged so far

  void reset(int maximum, float recharge = 0.0F);
  [[nodiscard]] bool recharging() const noexcept {
    return recharge_seconds > 0.0F;
  }
  [[nodiscard]] bool limited() const noexcept {
    return max > 0;
  }
  [[nodiscard]] bool empty() const noexcept {
    return max > 0 && ammo <= 0;
  }
  [[nodiscard]] bool refilling() const noexcept {
    return refill_left > 0.0F;
  }
  // Spends one round (nothing when unlimited).
  void take();
  // Adds `amount`, up to the maximum, and starts the count.
  void refill(int amount);
  // `resting`: a beam may recharge (none of it held or fading).
  void tick(float seconds, bool resting = true);
};

// Missiles and grenades wait for the fire control's lock sights (0x051045);
// guns and melee weapons use the target at once.
[[nodiscard]] inline bool needs_lock_time(const WeaponTuning &weapon) {
  return weapon.kind == WeaponKind::missile || weapon.kind == WeaponKind::grenade;
}

// The record aim mode (+0x1e) that locks: 0x09a663 gives a shoulder weapon
// lock sights (0x05289d's boxes, closing over the lock time, the lock-on
// warning and sound) only for it, the missiles. A grenade launcher (4) has
// none and fires along its mount.
inline constexpr int lock_on_aim_mode = 2;

// Whether the weapon has lock sights; a weapon without a record keeps its
// kind's (missiles and grenades).
[[nodiscard]] inline bool lock_sights(const WeaponTuning &weapon) {
  return weapon.aim_mode < 0 ? needs_lock_time(weapon) : weapon.aim_mode == lock_on_aim_mode;
}

// {"handgun": {"kind": "gun", ...}, ...}; throws on an unknown kind.
[[nodiscard]] std::map<std::string, WeaponTuning>
parse_weapon_tunings(const json::JsonValue &value);

// A body the weapons can hit: a vertical capsule approximated by a sphere
// at the robot's middle.
struct Target {
  Vec3 centre;
  float radius{};
  int team{};
  bool alive{};
  const StageCollision *collision{}; // placed scenery uses its extracted triangles
  bool on_robot{true};
  [[nodiscard]] std::optional<Vec3> hit_point(Vec3 from, Vec3 to, float fuse_radius) const;
  [[nodiscard]] float distance_to(Vec3 point) const;
};

struct Projectile {
  const WeaponTuning *weapon{};
  int owner{};
  int team{};
  int target{-1}; // missiles home on this robot
  Vec3 position;
  Vec3 velocity;
  float travelled{};
  float age{};
  std::uint64_t id{}; // stable across vector compaction; identifies persistent smoke trails
  int slot{-1};
};

// A beam held on its muzzle (0x09b9fe, battle_beams.cpp): its tip runs out
// at the weapon's speed to its range and flies on through walls; a robot it
// reaches is hit and the ray starts again. Let go, it fades out where it was.
struct Beam {
  const WeaponTuning *weapon{};
  int owner{};
  int slot{};
  int team{};
  Vec3 origin;
  Vec3 direction;          // a unit vector
  float length{};          // to the tip as it grows
  float reach{};           // drawn this far: to the robot it hit, or `length`
  float held{};            // seconds held, up to the fade; counts down once let go
  std::optional<Vec3> tip; // where the tip was last tick; none once it starts again
  bool holding{};          // held this tick
  float step_left{};       // seconds until the tip's next step (battle ticks, 30 Hz)
};

// A blast still growing: every battle tick it hurts each body it reaches,
// the one hit directly too.
struct Blast {
  const WeaponTuning *weapon{};
  int owner{};
  int team{};
  Vec3 position;
  float age{};
  float next_pulse{}; // the age at which it hurts again
};

// A blast or impact, drawn as an expanding flash.
struct Explosion {
  Vec3 position;
  float radius{};
  float age{};
  float life{0.5F};
};

// Where a shot or strike landed: on a body, a wall or the floor, or where
// a blast weapon's flight ended. The battle's sounds and effects hear it.
struct Landing {
  const WeaponTuning *weapon{};
  int owner{};
  Vec3 at;
  bool on_robot{}; // a body, so the weapon's robot_hit applies, not its surface_hit
  std::optional<StageHit> surface;
  int slot{-1};
  int target{-1};
};

// Whether a landing starts the weapon's impact effect (0x098eda).
[[nodiscard]] bool starts_impact(const WeaponTuning &weapon, bool on_robot) noexcept;

struct WeaponHit {
  int victim{};
  int attacker{};
  float damage{};
  float stagger{};
  Vec3 position;
  const WeaponTuning *weapon{};
  Vec3 direction;     // the way it pushes the victim (a unit vector)
  bool directional{}; // a shot or strike, which does more from behind; not a blast
  bool direct{};      // a blast on the body its round struck (counts as that round's hit)
};

// Every projectile and blast in flight. Battle fires the weapons and
// applies the hits this returns.
class Armory final {
  friend class StateInspector;
  friend class Battle;

public:
  // Fires `rounds` of `weapon` from `muzzle` along `aim` (a unit vector),
  // toward `target` when one is locked (-1 otherwise). Rounds after the
  // first scatter by up to the weapon's spread.
  void fire(const WeaponTuning &weapon, int owner, int team, Vec3 muzzle, Vec3 aim, int target,
            const std::vector<Target> &targets, int rounds = 1, int slot = -1);
  // A melee strike's armed blade at `blade` this tick (0x09c9c2): the
  // first robot of another team whose centre lies within its radius times
  // melee_contact_scale takes the weapon's damage. Returns whether it
  // connected; a strike connects once.
  bool strike(const WeaponTuning &weapon, int owner, int team, Vec3 blade,
              const std::vector<Target> &targets, std::vector<WeaponHit> &hits);
  // Holds the beam of `weapon` in `slot` on `muzzle` along `aim` (a unit
  // vector) for this tick.
  void hold_beam(const WeaponTuning &weapon, int owner, int slot, int team, Vec3 muzzle, Vec3 aim);
  // Moves everything on, collecting hits; shots stop at the stage's walls
  // and floor, and at the gates' shut door leaves.
  void tick(float seconds, const std::vector<Target> &targets, const StageCollision &stage,
            std::vector<WeaponHit> &hits, const StageDoors *doors = nullptr);

  [[nodiscard]] const std::vector<Projectile> &projectiles() const noexcept {
    return projectiles_;
  }
  [[nodiscard]] const std::vector<Beam> &beams() const noexcept {
    return beams_;
  }
  // Whether `owner`'s beam on `slot` is held or still fading out.
  [[nodiscard]] bool beam_live(int owner, int slot) const;
  [[nodiscard]] const std::vector<Explosion> &explosions() const noexcept {
    return explosions_;
  }
  // The landings since the last call.
  [[nodiscard]] std::vector<Landing> take_landings() {
    return std::exchange(landings_, {});
  }
  // A fireball that hurts no one (a wreck blowing up).
  void explode(const Vec3 at, const float radius, const float life) {
    explosions_.push_back({at, radius, 0.0F, life});
  }
  void clear();

private:
  [[nodiscard]] Vec3 launch_velocity(const WeaponTuning &weapon, Vec3 muzzle, Vec3 direction,
                                     const Target *locked) const;
  [[nodiscard]] Vec3 scatter(Vec3 direction, float degrees);
  void detonate(const Projectile &shot, Vec3 at, const std::vector<Target> &targets,
                std::vector<WeaponHit> &hits, bool on_robot = false,
                std::optional<StageHit> surface = std::nullopt, int target = -1);
  void tick_beams(float seconds, const std::vector<Target> &targets, const StageCollision &stage,
                  std::vector<WeaponHit> &hits, const StageDoors *doors);
  void spread_blasts(float seconds, const std::vector<Target> &targets,
                     std::vector<WeaponHit> &hits);

  std::vector<Projectile> projectiles_;
  std::uint64_t next_projectile_id_{1};
  std::vector<Blast> blasts_;
  std::vector<Beam> beams_;
  std::vector<Explosion> explosions_;
  std::vector<Landing> landings_;
  std::minstd_rand random_;
};

// The launch velocity that lobs a grenade from `from` onto `to` at
// `speed` under `gravity`, taking the higher arc when the target is out of
// reach of the lower one; aims straight at `to` if it cannot reach.
[[nodiscard]] Vec3 lob_velocity(Vec3 from, Vec3 to, float speed, float gravity);

} // namespace phantomcrash
