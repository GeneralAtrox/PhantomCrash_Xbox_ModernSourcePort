#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

// The wreck's swept stage contact, returning the position of its final explosion.
using SupplyContact = std::function<std::optional<Vec3>(Vec3, Vec3, float)>;

// The supply drops (0x066940 starts them with every battle, 0x06717c ticks
// them): a carrier flies one of its stage's routes, a chain of cubic Bezier
// segments (tables/battle_scenes.json "supply_routes"), opens its hatch on
// the drop segment and lets its pickups fall one after another (0x06804b).
// A pickup lands, waits out its lifetime blinking ever faster and bursts
// (0x069d52); a robot that touches it takes it first (0x0673db, 0x066e50).
// Once the carrier has flown the whole route it parks at the route's start
// and, when every pickup is used up, sets out on a new route picked at
// random (0x06a2f6). The original steps it at the battle's 30 Hz; the port
// moves it smoothly, in the same units a tick.

// What a pickup gives (0x066e50): money, a share of the armour back
// (0x06acc9), or a share of one weapon's magazine (0x06ad29).
enum class SupplyEffect { money, repair, ammo };

using SupplyColour = std::array<float, 3>;

// How a kind is drawn (port addition; docs/SUPPLY_BOXES.md). By default a
// kind is the original's crate of its own number, untinted, glowing in its
// effect's colour. `image` replaces the crate's picture, drawn on crate
// `crate`'s mesh; `tint` multiplies the picture and `label_tint` takes its
// place where the picture's alpha is below 1 (the lettering). The glow takes
// `glow` (else the tint, else the effect's colour), is glow_scale times as
// big and turns through every hue glow_cycle times a second. `sheen` adds a
// metal glint in the tint. A take plays `take_sounds` (none: the look's
// take_sound).
struct SupplyKindLook {
  std::string image;
  int crate{-1}; // -1: its own number's (an added kind: crate 0)
  std::optional<SupplyColour> tint;
  std::optional<SupplyColour> label_tint;
  std::optional<SupplyColour> glow;
  float glow_scale{1.0F};
  float glow_cycle{};
  float sheen{};
  std::vector<std::string> take_sounds;
};

struct SupplyKind {
  SupplyEffect effect{SupplyEffect::money};
  float amount{}; // credits, or the share of the armour or magazine
  float chance{}; // of each pickup carried
  SupplyKindLook look;
  std::optional<float> special_chance; // on the red carrier (none: `chance`)
};

// A pickup a robot took: what it gives, its type (0x21c378's index) and
// where it was.
struct SupplyTaken {
  SupplyKind kind;
  int type{};
  Vec3 at;
};

struct SupplyRoute {
  std::vector<std::array<Vec3, 4>> segments;
  int closing{}; // the segment the carrier closes in on (0x067bf6)
  int drop{};    // the segment it opens its hatch on (0x067f04)
  int leave{};   // the segment it turns away on
};

// The original's kinds, each with its own crate in the carrier models;
// data may append more (SupplySettings::kinds).
inline constexpr int supply_kind_count = 10;

struct SupplyPickupPulse {
  float frame_rate{};
  float step{};
  float fast_frames{};
  float fast_rate{};
  float life_numerator{};
  float colour_base{};
  float colour_steps{};
  float colour_divisor{};
};

// Optional asset-only pickup presentation. Each kind selects its authored
// texture, keeping the model's own UVs and samplers.
struct SupplyPickupLook {
  std::string model;
  std::array<std::string, supply_kind_count> images;
  float scale{2.5F};
  std::string carrier_model;
  std::string carrier_light_model;
  float light_spin_per_second{};
  std::optional<SupplyPickupPulse> pulse;
};
[[nodiscard]] SupplyPickupLook parse_supply_pickup_look(const json::JsonValue &value);

// How the drops are drawn (0x0699d5 the carrier, 0x069d52 the pickups).
// The carrier and its pickups share a model per stage parity (resources
// 207 and 208; the tables at 0x21c298..0x21c2b8 pick model slots 0x49e..
// and 0x4ad..), one mesh a slot: the body, on odd (night) stages a shell of
// lights drawn in a random colour every frame (0x4ae), the hatch's two
// doors, turned about their hinges by the hatch's angle, a part turned by
// the carrier's spin (a searchlight cone at night), the shot-down wreck
// drawn alone, and pickup kind k at first_pickup + k. (0x069d52 picks the
// day or night model's pickup at random each frame, and so does the port.)
struct SupplyModel {
  std::string model;
  int body{0};
  int shell{-1}; // none
  std::array<int, 2> doors{2, 3};
  int spinner{4};
  int wreck{1};
  int first_pickup{5};
};

struct SupplyLook {
  std::optional<SupplyPickupLook> pickups;
  std::array<SupplyModel, 2> models{SupplyModel{"models/binlink/207.gltf", 0, -1, {2, 3}, 4, 1, 5},
                                    SupplyModel{"models/binlink/208.gltf", 0, 1, {3, 4}, 5, 2, 6}};
  // Each door turns about the carrier's z through its hinge by the hatch's
  // angle times its turn (0x0699d5).
  std::array<Vec3, 2> door_hinges{Vec3{-0.735F, 0.163F, -2.736F}, Vec3{0.735F, 0.163F, 2.736F}};
  std::array<float, 2> door_turns{-1.0F, 1.0F};
  float pickup_scale{2.5F};
  // 0x069d52: on one screen a glow faces the camera over each pickup, its
  // alpha pulsing with the blink: texture slot 0x13f (resource 1574) on
  // grid_mdl's unit quad (model slot 0x4db) scaled by glow_size, pulled
  // glow_pull toward the camera, laid over what is behind it. On a split
  // screen the pickup is tinted instead, by the pulse times tint_pulse plus
  // tint_base.
  std::string glow_texture{"textures/binlink/1574.png"};
  float glow_size{3.7F};
  float glow_pull{1.8F};
  float glow_alpha{0x60 / 255.0F};
  float tint_pulse{0.8F};
  float tint_base{0.7F};
  // By effect (money, repair, ammo): 0x7070d0, 0xa0a0a0, 0x70d070.
  std::array<std::array<float, 3>, 3> glow_colours{
      std::array<float, 3>{0x70 / 255.0F, 0x70 / 255.0F, 0xd0 / 255.0F},
      std::array<float, 3>{0xa0 / 255.0F, 0xa0 / 255.0F, 0xa0 / 255.0F},
      std::array<float, 3>{0x70 / 255.0F, 0xd0 / 255.0F, 0x70 / 255.0F}};
  // 0x087eac: a take bursts effect group take_effects[effect] from the
  // pickup to the robot, thrown for one frame at take_rate times the
  // pickup's level (its place among its effect's kinds, from 1) a second.
  // At most take_bursts at once (0x966c0c).
  std::array<int, 3> take_effects{0x31, 0x32, 0x33};
  float take_rate{2400.0F};
  int take_bursts{3};
  float crash_radius{6.0F}; // the wreck's fireball (effect 0x3f; port size)
  float crash_seconds{1.0F};
  int crash_effect{0x3f};                // the effect group 0x06828e sets off
  int burst_effect{0x40};                // a shot/expired pickup, 0x069d52 state 3
  std::string take_sound{"supply_take"}; // battle_player 0x26, to a player's robot
};

// The red carrier (port addition; docs/SUPPLY_BOXES.md): once `streak`
// carriers in a row are shot down before dropping anything, the next comes
// tinted `tint` and `scale` times as big, pays shot_bounty when shot down
// and drops a pickup onto each of `spots` (x, z), drop_ticks apart, each
// kind rolled by its special_chance. A streak of 0 or no spots: never.
struct SupplySpecial {
  int streak{};
  SupplyColour tint{0.9F, 0.1F, 0.1F};
  float scale{1.0F};
  float drop_ticks{12.0F};
  std::vector<std::array<float, 2>> spots;
  float shot_bounty{1000.0F};
  std::string arrive_sound; // named sound (data/audio/sfx.json) as it sets out
};

// The rescue (port addition; docs/SUPPLY_BOXES.md): a player's robot below
// armour_share of its armour as a carrier sets out is brought a repair box
// giving at least min_repair, first out of the hatch, gliding toward the
// robot as it falls by up to `glide` across a tick so that it lands as near
// the robot as it can. An armour_share of 0: never.
struct SupplyRescue {
  float armour_share{};
  float min_repair{0.5F};
  float glide{1.5F};
};

struct SupplySettings {
  float tick_rate{30.0F};       // the battle's logic rate the counts below run at
  float speed{0.03F};           // of a segment a tick (0x066940)
  int pickups{3};               // carried each trip (0x0679e7)
  float drop_ticks{50.0F};      // between one pickup falling and the next (0x066ccd)
  float hatch_open{0.7853982F}; // the hatch's angle, opened (0x06804b)
  float hatch_speed{0.03F};     // of angle a tick, opening and closing
  float spin_speed{0.03F};      // the carrier's spin a tick, radians (0x067bf6)
  Vec3 hold{0.0F, 0.5F, -2.7F}; // a carried pickup, in the carrier's frame (0x966890)
  float fall_speed{0.5F};       // a tick (0x069d52)
  float rest_height{1.0F};      // a landed pickup's height above the ground
  float lifetime_ticks{900.0F}; // how long a landed pickup waits
  float reach{1.0F};            // taken within a robot's radius and this (0x0673db)
  // An ammo box refills the weapon with the smallest share of its magazine
  // left (port addition, "ammo_to": "emptiest"); the original picks one of
  // those short of rounds at random (0x06ad29).
  bool ammo_emptiest{};
  // Shots (0x067603): one brings the carrier down within carrier_radius and
  // pays its shooter shot_bounty (0x047ed9); one bursts a pickup within
  // pickup_radius. The wreck keeps wreck_keep of its speed and sinks
  // wreck_sink a tick after the immediate explosion. It explodes again on
  // stage contact or after wreck_ticks (0x06781a, 0x06828e).
  float carrier_radius{3.0F};
  float pickup_radius{1.0F};
  float shot_bounty{1000.0F};
  float wreck_keep{0.6F};
  float wreck_sink{0.3F};
  float wreck_ticks{500.0F};
  float blink_fast_ticks{60.0F}; // blinks at its fastest from this much life left
  float blink_fast{15.0F};       // blink rate then; before, lifetime / life left
  float blink_step{0.05F};       // of the blink's half cycle a tick, at rate 1
  // What each pickup kind gives and its chance (0x066b91 rolls per pickup;
  // stages 6 and 7 take set 6 of the table at 0x24b3cc). A pickup's kind is
  // its index here, so data only ever appends kinds.
  std::vector<SupplyKind> kinds{SupplyKind{SupplyEffect::money, 100.0F, 0.08F},
                                SupplyKind{SupplyEffect::money, 500.0F, 0.12F},
                                SupplyKind{SupplyEffect::money, 1000.0F, 0.08F},
                                SupplyKind{SupplyEffect::money, 10000.0F, 0.02F},
                                SupplyKind{SupplyEffect::repair, 0.1F, 0.15F},
                                SupplyKind{SupplyEffect::repair, 0.3F, 0.1F},
                                SupplyKind{SupplyEffect::repair, 0.5F, 0.1F},
                                SupplyKind{SupplyEffect::ammo, 0.3F, 0.06F},
                                SupplyKind{SupplyEffect::ammo, 0.5F, 0.15F},
                                SupplyKind{SupplyEffect::ammo, 1.0F, 0.14F}};
  std::vector<SupplyRoute> routes; // the stage's; none: no supply drops
  bool stage_routes{};             // includes an explicitly empty imported stage route list
  SupplyLook look;
  SupplySpecial special;
  SupplyRescue rescue;
};

// data/world/training.json "supply"; missing values keep the original's.
// The routes come from parse_supply_routes.
[[nodiscard]] SupplySettings parse_supply_settings(const json::JsonValue &value);
// tables/battle_scenes.json "supply_routes" of `stage`.
[[nodiscard]] std::vector<SupplyRoute> parse_supply_routes(const json::JsonValue &document,
                                                           int stage);
[[nodiscard]] std::vector<SupplyRoute> parse_supply_route_list(const json::JsonValue &value);
// The weapon an ammo box refills, given the share of its magazine each
// weapon short of rounds has left: the emptiest (the first of equals) or,
// as the original, one at random.
[[nodiscard]] std::size_t pick_ammo_slot(const std::vector<float> &shares, bool emptiest,
                                         std::minstd_rand &random);
// The point `along` (0..1) a cubic Bezier segment (0x067dd6).
[[nodiscard]] Vec3 bezier_point(const std::array<Vec3, 4> &segment, float along);

// The item states of the pickup table (0x9668a0 +0x14).
enum class SupplyState { carried, falling, landed, burst, gone };

struct SupplyPickup {
  Vec3 position;
  int kind{};
  SupplyState state{SupplyState::carried};
  float wait{};   // ticks before it may fall
  float life{};   // ticks left once landed
  float blink{};  // the blink's phase, 0..pi
  int spot{-1};   // the red carrier's: the special spot it falls onto
  int target{-1}; // a rescue box's: the robot it glides toward
};

struct SupplyCarrier {
  Vec3 position;
  float heading{}; // turned along its flight
  float hatch{};   // 0 shut .. hatch_open
  float spin{};
  bool parked{};
  bool wrecked{}; // shot down, falling
  float scale{1.0F};
  std::optional<SupplyColour> tint; // the red carrier's
};

class SupplyDrop final {
  friend class StateInspector;
  friend class Battle;

public:
  SupplyDrop(SupplySettings settings, std::uint32_t seed);

  [[nodiscard]] bool active() const noexcept {
    return !settings_.routes.empty();
  }
  // `ground` answers the ground's height under a point.
  void tick(float seconds, const std::function<float(Vec3)> &ground,
            const SupplyContact &contact = {});
  // Where each robot stands, in the battle's order, and the robots calling
  // for a rescue box; the next carrier to set out brings them one each.
  void watch(std::vector<Vec3> robots, std::vector<int> rescue);
  // The kind a rescue box is: the weakest repair giving at least
  // min_repair, else the strongest repair; -1 with no repair kind.
  [[nodiscard]] int rescue_kind() const;
  // The pickup a robot whose collision sphere is centred at `centre` with
  // `radius` touches, taken; its kind, or nothing.
  [[nodiscard]] std::optional<SupplyTaken> take(Vec3 centre, float radius);
  // Whether a shot can bring the carrier down now, and doing so: true when
  // it went down. A shot bursts a falling or landed pickup.
  [[nodiscard]] bool carrier_shootable() const noexcept {
    return shootable_ && !carrier_.parked && !carrier_.wrecked;
  }
  bool shoot_carrier();
  void shoot_pickup(std::size_t index);
  // The carrier's reach for shots and contact, and what bringing it down pays.
  [[nodiscard]] float carrier_radius() const noexcept {
    return settings_.carrier_radius * carrier_.scale;
  }
  [[nodiscard]] float shot_bounty() const noexcept {
    return special_ ? settings_.special.shot_bounty : settings_.shot_bounty;
  }
  // Whether this trip is the red carrier's, the carriers shot down empty in
  // a row toward it, and whether it set out this tick (cleared on asking).
  [[nodiscard]] bool special() const noexcept {
    return special_;
  }
  [[nodiscard]] int streak() const noexcept {
    return streak_;
  }
  [[nodiscard]] bool take_special_arrival() noexcept {
    return std::exchange(arrived_, false);
  }
  // The wreck's crash this tick (0x06828e sets off effect 0x3f there).
  [[nodiscard]] std::optional<Vec3> take_crash() noexcept {
    return std::exchange(crash_, std::nullopt);
  }
  // Completed pickup bursts, retained until the battle submits their effects.
  [[nodiscard]] std::vector<Vec3> take_bursts() noexcept {
    return std::exchange(bursts_, {});
  }
  // A pickup's blink, 0..1 (|sin| of its phase).
  [[nodiscard]] static float blink(const SupplyPickup &pickup);

  [[nodiscard]] const SupplyCarrier &carrier() const noexcept {
    return carrier_;
  }
  [[nodiscard]] const std::vector<SupplyPickup> &pickups() const noexcept {
    return pickups_;
  }
  [[nodiscard]] const SupplySettings &settings() const noexcept {
    return settings_;
  }

private:
  enum class Phase { inbound, closing, dropping, outbound, parked, wreck };
  enum class Hatch { shut, opening, dropping, closing };

  void start_route();
  void load();
  void fly(float ticks);
  void work_hatch(float ticks);
  void fall(float ticks, const std::function<float(Vec3)> &ground, const SupplyContact &contact);
  void park();
  void start_special();
  void steer_to_spot(SupplyPickup &pickup, float ticks, const std::function<float(Vec3)> &ground);
  void load_rescue();
  void glide_to_target(SupplyPickup &pickup, float ticks, const std::function<float(Vec3)> &ground);
  [[nodiscard]] int roll_kind(float value) const;
  void tick_pickups(float ticks, const std::function<float(Vec3)> &ground);
  [[nodiscard]] Mat4 carrier_frame() const;

  SupplySettings settings_;
  std::minstd_rand random_;
  SupplyCarrier carrier_;
  std::vector<SupplyPickup> pickups_;
  const SupplyRoute *route_{};
  int segment_{};
  float along_{};
  Phase phase_{Phase::parked};
  Hatch hatch_{Hatch::shut};
  bool shootable_{};   // 0x966c30, cleared by the shot that brings it down
  Vec3 velocity_;      // a tick
  float wreck_left_{}; // ticks
  std::optional<Vec3> crash_;
  std::vector<Vec3> bursts_;
  int streak_{};             // carriers shot down in a row before dropping anything
  bool special_{};           // this trip is the red carrier's
  bool arrived_{};           // the red carrier set out, not yet reported
  std::vector<Vec3> robots_; // where each robot stands (watch)
  std::vector<int> rescue_;  // the robots calling for a rescue box
};

} // namespace phantomcrash
