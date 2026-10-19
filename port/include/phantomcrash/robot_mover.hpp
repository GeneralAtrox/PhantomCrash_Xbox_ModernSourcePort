#pragma once

#include "phantomcrash/root_motion.hpp"

#include "phantomcrash/movement_tuning.hpp"
#include "phantomcrash/math3d.hpp"
#include "phantomcrash/robot_motion.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace phantomcrash {

struct MotionTiming; // robot.hpp

// How a jump of a given height goes (0x075a36, 0x08ea5b, 0x0905af): the
// robot leaves the ground at `speed` (robot +0x2560), a share of the speed
// that would throw it the whole height, plus a tick of gravity. While the
// jump stays held its vertical speed eases toward `speed` and the lift
// cancels gravity for `lift_seconds` (the budget at +0x2564, spent a tick
// of gravity a tick), long enough to rise a share of the height; the throw
// after it covers the rest. A tap rises only that throw.
struct JumpTakeoff {
  float speed{};        // units a second
  float lift_seconds{}; // the most the lift can hold
};
inline constexpr float jump_takeoff_share = 0.4F; // 0x17e354
inline constexpr float jump_lift_share = 0.84F;   // 0x1b5ab0
inline constexpr float jump_lift_ease = 0.1F;     // 0x1b5b70, of the take-off speed a tick
[[nodiscard]] JumpTakeoff jump_takeoff(float height, float gravity);
// How far a jump held to the top rises, from the ground.
[[nodiscard]] float held_jump_rise(const MovementTuning &tuning);
// The jump motion's landing: a press this soon after landing is ignored.
inline constexpr float landing_seconds = 0.25F;

// A melee strike as the fire code starts it (0x09c337): the arm's strike
// motion (strike_motion) and its frames from the leg type's timing table,
// and where the locked robot is when the lock has completed (the lunge).
struct StrikeStart {
  int motion{};
  float armed_frames{}; // the blade disarms here (the motion's loop_end, +0x211c)
  float last_frames{};  // and the strike ends here (its last, +0x2120)
  std::optional<Vec3> lunge_at;
  bool piloted{}; // robot +8 not -1: the player's lunge turn and speed
  bool source_free_arm_action{};
};

// The strike motion of an arm weapon (0x09dd24): by the weapon record's
// model (+8), 0x19 for the left arm and 0x1a for the right with model 7,
// 0x17 and 0x18 with any other; 10 later in the air (0x09c337).
inline constexpr int strike_motion_model = 7;
[[nodiscard]] int strike_motion(int weapon_model, int side, bool grounded);

// One tick of a robot's controls, from the pad or from the AI.
struct RobotControl {
  std::array<float, 2> move{};      // steering yaw and forward travel (AI and legacy controls)
  float strafe{};                   // independent lateral travel, positive right
  float twist{};                    // right stick x: turn the upper body
  float pitch{};                    // right stick y: aim up and down
  std::array<float, 2> mouse_aim{}; // direct yaw/pitch displacement, radians
  bool jump{};                      // A held
  bool dash{};                      // left stick clicked this tick
  int dodge{};                      // -1 left, +1 right (white / black pressed)
  bool reset_twist{};               // right stick clicked this tick
  bool down_boost{};                // an airborne fast-descent request
  bool standard{};                  // the Standard control type's stick rules
  bool quick_turn{};                // twist is Preferred's BEGINNER MODE quick turn
};

enum class MoveMode {
  stand,
  walk,
  turn,
  dash,
  jump,
  dodge,
  brake,
  hit,
  strike,
  scripted,
  destroyed
};

// One tick of a scripted walk (the battle's entrance and exit, 0x08928c and
// 0x08a161): the robot moves at `velocity` along the ground facing
// `heading` and plays `motion` at `motion_speed`; its controls do nothing.
struct ScriptedStep {
  Vec3 velocity; // world units a second
  float heading{};
  int motion{robot_motion::stand};
  bool loops{true};
  float motion_speed{1.0F};
};

// A robot's movement over the ground: walking, turning on the spot,
// dashing, jumping, dodging and the upper-body twist, with the leg motion
// each state plays. Collision is left to the caller (Battle).
class RobotMover final {
  friend class StateInspector;
  friend class Battle;

public:
  explicit RobotMover(const MovementTuning &tuning) : tuning_(tuning) {}
  void set_tuning(const MovementTuning &tuning) noexcept {
    tuning_ = tuning;
  }
  [[nodiscard]] const MovementTuning &tuning() const noexcept {
    return tuning_;
  }

  // The leg type's motion frames (robot.hpp), which time the dodge, and
  // its index; Battle points them at the robot's assembly before each tick.
  void set_timings(const std::vector<MotionTiming> *timings, const int leg_type = -1) noexcept {
    timings_ = timings;
    leg_type_ = leg_type;
  }
  void place(Vec3 position, float heading);
  void tick(const RobotControl &control, float seconds, const RootMotionSource &root = {});
  // A hit that pushes the robot along `push` (world space): it plays the
  // hit reaction for that direction (0x08dcd1) for `seconds`, at most
  // hit_seconds, stopped on the ground and falling uncontrolled in the air.
  void stagger(float seconds, Vec3 push);
  // Starts a melee strike (0x09c337), unless one is under way.
  bool strike(const StrikeStart &start);
  // Where the lunging strike's target is this tick; none once it is gone.
  void follow_lunge(std::optional<Vec3> target);
  // Whether a strike is under way, and whether its blade is armed.
  [[nodiscard]] bool striking() const noexcept {
    return mode_ == MoveMode::strike;
  }
  [[nodiscard]] bool blade_armed() const noexcept;
  // Drives the robot for the next tick (see ScriptedStep).
  void script(const ScriptedStep &step);
  // Hands the robot back to its controls, standing.
  void release();
  void destroy();

  [[nodiscard]] Vec3 position() const noexcept {
    return position_;
  }
  void set_position(Vec3 position) noexcept {
    position_ = position;
  }
  // The ground height under the robot, for the next tick (no_ground: none).
  void set_floor(float height) noexcept {
    floor_ = height;
  }
  // Stands the robot on ground at `height` (a surface it was pushed onto).
  void land(float height);
  // Stops the rise of a jump (a ceiling).
  void bump_head();
  [[nodiscard]] float heading() const noexcept {
    return heading_;
  }
  [[nodiscard]] float twist() const noexcept {
    return twist_;
  }
  // The twist stick's push this tick (robot +0x2f68), -1..1.
  [[nodiscard]] float twist_push() const noexcept {
    return twist_push_;
  }
  [[nodiscard]] float pitch() const noexcept {
    return pitch_;
  }
  // Where the upper body faces: heading plus twist.
  [[nodiscard]] float aim_heading() const noexcept {
    return heading_ + twist_;
  }
  [[nodiscard]] MoveMode mode() const noexcept {
    return mode_;
  }
  // The last tick's steering and forward stick (RobotControl::move).
  [[nodiscard]] const std::array<float, 2> &stick() const noexcept {
    return stick_;
  }
  // The native control adapter for source wheel steering. Wheeled controls
  // combine lateral bindings; this does not include reverse/boost turn scaling.
  [[nodiscard]] float rolling_steer() const noexcept {
    return rolling_steer_;
  }
  [[nodiscard]] bool backward() const noexcept {
    return backward_;
  }
  [[nodiscard]] bool grounded() const noexcept {
    return grounded_;
  }
  [[nodiscard]] bool down_boosting() const noexcept {
    return down_boosting_;
  }
  // Walking only sideways this tick: the original's strafe (0x08dede) slides
  // the robot without changing its state or motion, so it takes no steps.
  [[nodiscard]] bool strafing() const noexcept {
    return strafing_;
  }
  // Velocity in the robot's own frame: +z forward, +x right.
  [[nodiscard]] Vec3 local_velocity() const noexcept {
    return local_velocity_;
  }
  [[nodiscard]] Vec3 velocity() const;
  // The leg motion for the current state and whether it loops.
  [[nodiscard]] int motion() const noexcept {
    return motion_;
  }
  [[nodiscard]] std::uint64_t motion_revision() const noexcept {
    return motion_revision_;
  }
  [[nodiscard]] bool motion_loops() const noexcept {
    return loops_;
  }
  // Motion playback speed (the walk slows with a half-pressed stick).
  [[nodiscard]] float motion_speed() const noexcept {
    return motion_speed_;
  }
  // The dodge taken this tick (-1, +1) or 0.
  [[nodiscard]] int dodged() const noexcept {
    return dodged_;
  }
  [[nodiscard]] float mode_seconds() const noexcept {
    return mode_seconds_;
  }
  // A fresh jump press would take off on the next tick: on the ground, the
  // jump released, and not landing, staggered, dodging or striking.
  [[nodiscard]] bool jump_ready() const noexcept;

private:
  void set_mode(MoveMode mode, int motion, bool loops, bool restart = false);
  void tick_ground(const RobotControl &control, float seconds, const RootMotionSource &root);
  bool stop_root_walk(const RobotControl &control, const RootMotionSource &root);
  void tick_root_coast(const RobotControl &control, float seconds);
  void apply_root_stride(const RobotControl &control, float seconds, const RootMotionSource &root);
  void tick_wheels(const RobotControl &control, float seconds);
  void steer_dash(float side, float seconds);
  void tick_air(const RobotControl &control, float seconds);
  void tick_twist(const RobotControl &control, float seconds);
  void tick_quick_turn(const RobotControl &control, float seconds);
  void tick_strike(float seconds);
  void lunge(float seconds);
  void turn(float stick, float degrees_per_second, float seconds);
  [[nodiscard]] int flick(float stick, float seconds);
  void start_dodge(int side);
  [[nodiscard]] bool tick_dodge(float seconds);

  MovementTuning tuning_;
  Vec3 position_{};
  float heading_{};
  float twist_{};
  float mouse_yaw_{};   // radians the mouse asked to turn, still to turn
  float twist_stick_{}; // the stick's smoothed push, 0..1
  float twist_push_{};
  float twist_reset_from_{};
  float twist_reset_left_{};
  float quick_turn_left_{};     // radians still to turn (+0x2748)
  float quick_turn_cooldown_{}; // seconds (+0x2744)
  float pitch_{};
  MoveMode mode_{MoveMode::stand};
  int motion_{robot_motion::stand};
  std::uint64_t motion_revision_{};
  bool loops_{true};
  float motion_speed_{1.0F};
  float mode_seconds_{};
  bool backward_{};
  bool grounded_{true};
  float floor_{};
  Vec3 local_velocity_{};
  Vec3 coast_velocity_{}; // stopping momentum in world space (robot +0x2584)
  bool coast_brake_{};    // the idle callback starts the brake after a completed stride
  float vertical_speed_{};
  float dash_hold_{};
  float lift_left_{};  // the held jump's lift still to come, seconds
  float lift_spent_{}; // and what it has used, seconds
  bool down_boosting_{};
  bool strafing_{};
  bool jump_was_held_{};
  std::array<float, 2> stick_{};
  float rolling_steer_{};
  float stagger_left_{};
  float strike_armed_frames_{};
  float strike_last_frames_{};
  std::optional<Vec3> lunge_at_;
  float lunge_height_{}; // the target's height when the strike began (+0x25b4)
  bool lunge_piloted_{};
  int dodged_{};
  int flick_state_{};  // +0x2538
  float flick_left_{}; // +0x253c, seconds
  const std::vector<MotionTiming> *timings_{};
  int leg_type_{-1};
  float dash_steer_{};    // +0x257c, the eased steer that leans a dash
  Vec3 dodge_velocity_{}; // the push in the robot's frame (+0x256c)
  float dodge_slide_frames_{};
  float dodge_ease_frames_{}; // the motion's loop end (+0x211c) ...
  float dodge_last_frames_{}; // ... and its last frame (+0x2120)
};

// The original runs battles at 30 ticks a second (0x255ab0, scene 42).
inline constexpr float battle_tick_rate = 30.0F;

// Ground this far below the feet still holds a walking robot (port value).
inline constexpr float ground_snap = 0.5F;

// The unit vector a heading faces along the ground: robots face -z of
// their own frame (the walk moves them by (0, 0, -speed), 0x08e6b6) and
// RotY(heading) turns that frame.
[[nodiscard]] Vec3 heading_forward(float heading);
// The heading that faces along `direction` (the inverse of heading_forward).
[[nodiscard]] float heading_toward(Vec3 direction);
// The robot's right-hand side for `heading`.
[[nodiscard]] Vec3 heading_right(float heading);
// `angle` wrapped into -pi..pi.
[[nodiscard]] float wrap_angle(float angle);

} // namespace phantomcrash
