#pragma once

#include "phantomcrash/json.hpp"

#include <optional>
#include <vector>

namespace phantomcrash {

// Opt-in native wheeled handling. All six values come from a leg record's
// movement data; they are port tuning, not recovered S.L.A.I. constants.
struct WheeledTuning {
  float acceleration{};
  float braking{};
  float lateral_grip{};
  float turn_scale{};
  float reverse_turn_scale{};
  float radius{};
};

[[nodiscard]] std::optional<WheeledTuning> parse_wheeled_tuning(const json::JsonValue *value);

// How a robot moves (data/world/training.json "movement"). A robot with
// legs records takes its speeds from them (robot_load.hpp); the walk and
// dash speeds here are then its motions' strides, or are replaced, and
// stay port values only for a robot without legs records.
struct MovementTuning {
  std::optional<WheeledTuning> wheels;
  float walk_speed{7.5F}; // units per second at full stick
  float back_speed{5.5F};
  float walk_accel{30.0F}; // units per second squared
  float stop_keep{0.875F}; // leg record +0xbc, retained momentum per battle tick
  float stop_speed{6.0F};  // 0x090d21 stops below 0.2 units per 30 Hz tick
  float dash_speed{24.0F};
  float dash_back_speed{18.0F};
  // How fast the walk and dash motions play (robot +0x68, +0x70): the
  // robot's load sets them (robot_load.hpp) and the speeds above with them.
  float walk_motion_rate{1.0F};
  float dash_motion_rate{1.0F};
  // Whether the walk and dash speeds above are their motions' strides
  // (stride_tuning) rather than the defaults.
  bool walk_stride{};
  bool dash_stride{};
  // The turns on the ground, in a dash and in the air. The original turns
  // at one rate in all three (0x08e257 reads robot +0x7c, 0x0905af +0x78,
  // both the legs' turn under the load); robot_load sets them from the
  // legs. These defaults are for a robot without legs records.
  float turn_degrees_per_second{110.0F};
  float dash_turn_degrees_per_second{70.0F};
  float air_turn_degrees_per_second{55.0F};
  float stick_threshold{0.01F};   // 0x199f78: the stick counts as pushed past this
  float dash_stick{0.7F};         // 0x17cd14: holding past this ...
  float dash_hold_seconds{0.5F};  // ... for 30 frames (0x17ccfc) starts a dash
  float brake_cut_seconds{0.35F}; // the stick may cut a dash's stop short after this
  // 0x08f5d8: the leg types (0x05f788) whose dashes lean into a turn (their
  // movement-sound family, 0x0823cb, is 3 to 6, or they are type 4, not 20),
  // past this eased steer (0x19b550), eased a sixteenth a tick (0x17d0e4).
  std::vector<int> dash_turn_legs{2, 3, 4, 8, 9, 13, 14, 15};
  float dash_turn_stick{0.3F};
  float dash_turn_ease_ticks{16.0F};
  // The Standard controls (0x0774ad, robot +0x2740 bit 0). The left stick
  // pushed past `flick_stick` to one side, let go and pushed there again,
  // each within `flick_seconds`, dodges to that side. The requested native
  // controls retain strafing in the air. Disable standard_air_strafe for
  // the original left-stick turning with right-stick priority.
  bool standard_air_strafe{true};
  float flick_stick{0.5F};               // 0x17accc, 0x17be84
  float flick_seconds{5.0F / 30.0F};     // 5 ticks (+0x253c)
  float standard_air_turn_stick{0.001F}; // 0x19f8d0
  // Preferred with BEGINNER MODE (0x07d330, robot +0x273c): the right stick
  // past quick_turn_stick queues a turn of quick_turn_degrees, taken at up
  // to quick_turn_share of the twist rate; the left stick cancels it and
  // the opposite push stops it for quick_turn_cooldown_seconds.
  float quick_turn_stick{0.5F};                    // 0x17accc, 0x17be84
  float quick_turn_degrees{90.0F};                 // 0x3fc90fdb, pi/2
  float quick_turn_share{0.8F};                    // 0x19b528
  float quick_turn_cooldown_seconds{8.0F / 30.0F}; // 8 ticks (+0x2744)
  // The jump (robot +0x74): the height a held jump reaches. The take-off
  // speed and the lift that holds it up follow from it (jump_takeoff).
  float jump_height{16.0F};
  // 0x0905af / 0x04476d: the world's 6.0 times 9.8, in addition to gravity.
  float down_boost_acceleration{58.8F};
  // 0x07d117: 9.8 / (rate / 2)^2 a tick at the battle's 30 Hz, times the
  // world's 2.5 (0x046aa5); 9.8 * 2.5 units/s^2.
  float gravity{24.5F};
  float air_control{0.5F};      // share of the walk speed steerable in the air
  float dodge_speed{32.0F};     // units a second; the legs' push (robot_load.hpp)
  float air_dodge_speed{32.0F}; // the same in the air
  // The dodge runs on its motion's frames (0x08ef79): the robot goes on as
  // it moved until the loop start, slides on the push, which keeps
  // `dodge_keep` of itself a tick, until the loop end, and slows to the
  // last frame: on the ground to the push times `dodge_ease` for each frame
  // left plus one, in the air keeping `air_dodge_keep` of its speed a tick.
  float dodge_keep{0.99F};    // 0x1b5ac0
  float dodge_ease{0.05F};    // 0x17bec4
  float air_dodge_keep{0.9F}; // 0x1b5af4
  float dodge_seconds{0.3F};  // the slide when a motion has no frames (port value)
  // A melee strike (mover state 9, 0x08fc1e) plays its strike motion
  // (StrikeStart). While its blade is armed (phases 1 and 2, robot
  // +0x25a5) the robot steps forward 0.25 units a frame (0x199fd4). One
  // whose lock has completed lunges at the locked robot instead (0x091245):
  // it turns toward it by up to lunge_turn_degrees a tick (0x1b5b4c; a
  // computer robot 0x1b5b78) and moves the whole way there in a tick, but
  // no faster than its dash motion's rate times lunge_rate_speed (robot
  // +0x25bc, 0x075a36: twice the rate, 1.4 times for a computer robot
  // (0x1b59d4), in units a frame), and not at all while it faces more than
  // lunge_cone_degrees away (0x19f870). It climbs toward the target's
  // height at a tenth of the gap a frame (0x17cd04), within the jump's
  // take-off speed, and gravity does not pull it.
  float strike_speed{15.0F};
  float lunge_rate_speed{120.0F};
  float computer_lunge_rate_speed{84.0F};
  float lunge_turn_degrees{11.25F}; // a battle tick
  float computer_lunge_turn_degrees{7.875F};
  float lunge_cone_degrees{90.0F};
  float lunge_climb_rate{6.0F}; // of the height between them, a second
  // A flinch lasts its stun, at most as long as the hit reactions' motions,
  // 29 frames (motions 17-20 and 29-32; 0x07675e caps robot +0x25a0).
  float hit_seconds{29.0F / 60.0F};
  // The twist (robot +0xf8, 0x07d330 with the Standard controls): the
  // legs' turn (robot +0x7c) times the 60 Hz frames a tick and the stick,
  // so loaded_movement sets the rate; within 45 degrees (0x17d0e0). The
  // stretch, the stick's smoothed push (keeping twist_stick_keep a tick,
  // 0x1b5af4) times twist_stretch_degrees past the limit, is the camera's
  // look (0x1b5af0) and stays 0 here. The reset eases it back to straight
  // over 8 ticks (0x17beec).
  float twist_degrees_per_second{90.0F};
  float twist_limit_degrees{45.0F};
  float twist_stretch_degrees{0.0F};
  float twist_stick_keep{0.9F}; // per battle tick
  float twist_reset_seconds{8.0F / 30.0F};
  // The aim pitch follows the camera's look (+0x118 +0x40): 3 degrees a
  // battle tick times the stick (0x19f8f8), with a 45-degree camera limit
  // (0x07fd72). The separate +-0.4 rad clamp in 0x0aa499 is for body animation.
  float pitch_limit{0.7853982F};
  float pitch_per_second{1.5707964F};
};

[[nodiscard]] MovementTuning parse_movement_tuning(const json::JsonValue &value);

} // namespace phantomcrash
