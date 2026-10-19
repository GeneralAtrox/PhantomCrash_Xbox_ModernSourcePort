#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/robot_mover.hpp"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

// What a lesson step waits for, as the robot state handlers at 0x242d38
// check it (states 0x18-0x2e; researcher-assigned names).
enum class LessonGoal {
  none,           // the step only talks
  walk_both_ways, // walk `amount` forward and backward (0x08b5c3)
  turn_right,     // turn on the spot through `amount` radians (0x08b815)
  turn_left,      // (0x08b9dd)
  dash,           // dash `amount` units (0x08bbb7)
  jump,           // any jump, landed (0x08bd7d)
  high_jump,      // a jump higher than `amount` (0x08befe)
  dodge_both,     // `amount` dodges each way (0x08c0c1)
  twist_both,     // twist the upper body past +-`amount` radians (0x08c294)
  twist_reset,    // back to straight (0x08c43a)
  kills,          // destroy `amount` robots (0x08c792, 0x08cdce)
  camouflage,     // turn Optic Camouflage on (0x08d151)
};

struct LessonStep {
  int state{};
  LessonGoal goal{LessonGoal::none};
  float amount{};
  bool drones{};          // the drones walk while the step is practised (0x091ff4)
  unsigned weapons{};     // WeaponSlot bits the player may fire
  float walk_limit{1.0F}; // the forward stick is clamped to +-this (0x08b5c3: 0.7)
  int turn_only{};        // +1 or -1: the sideways stick only counts that way
  float place_drone{};    // > 0: a drone is put this far ahead (0x08d09e: 30)
  bool end{};             // the battle ends after this step's message
};

// The lessons by the mode Pepper's guide stores (save +0x10f10): "0" basic
// controls, "1" attack methods, "2" Optic Camouflage. FreePlay (3) has
// none.
[[nodiscard]] std::map<int, std::vector<LessonStep>>
parse_lesson_courses(const json::JsonValue &value);

// What the robot did in one tick, for the goals.
struct LessonSample {
  MoveMode mode{MoveMode::stand};
  bool grounded{true};
  Vec3 local_velocity{}; // +z forward, units per second
  float seconds{};
  float heading_change{}; // radians this tick, + to the right
  float height{};
  int dodged{};
  float twist{};
  int kills{};
  bool camouflaged{};
};

// Runs one course: each step plays its first message (controls locked),
// then waits for its goal, then plays its second message and moves on
// (0x091f8f picks the messages; 0x092070 locks the controls).
class Tutorial final {
public:
  enum class Phase { intro, practice, outro, finished };

  explicit Tutorial(std::vector<LessonStep> steps);

  // The message to play now: {state, 0 first / 1 second}. The screen plays
  // it and calls message_done(); a state without that message skips it.
  [[nodiscard]] std::optional<std::pair<int, int>> message() const;
  void message_done();
  void update(const LessonSample &sample);

  [[nodiscard]] Phase phase() const noexcept {
    return phase_;
  }
  [[nodiscard]] const LessonStep *step() const;
  [[nodiscard]] bool finished() const noexcept {
    return phase_ == Phase::finished;
  }
  [[nodiscard]] bool practising() const noexcept {
    return phase_ == Phase::practice;
  }
  // Progress toward the goal, 0..1, for the HUD.
  [[nodiscard]] float progress() const;
  // Kills counted toward the current goal.
  [[nodiscard]] int kills() const noexcept {
    return kills_now_ - kills_start_;
  }
  // Set when a step starts that places a drone; cleared by the screen.
  [[nodiscard]] bool take_place_drone() noexcept {
    return std::exchange(place_drone_, false);
  }

private:
  void start_practice();
  void next_step();
  [[nodiscard]] bool goal_met() const;

  std::vector<LessonStep> steps_;
  std::size_t index_{};
  Phase phase_{Phase::intro};
  // The goal's measures (the robot's scratch words at +0x2764.. in the original).
  float forward_{};
  float backward_{};
  float turned_{};
  float distance_{};
  float peak_{};
  bool airborne_{};
  bool landed_{};
  int dodges_left_{};
  int dodges_right_{};
  bool twisted_right_{};
  bool twisted_left_{};
  float twist_{};
  bool camouflaged_{};
  int kills_start_{};
  int kills_now_{};
  bool place_drone_{};
};

} // namespace phantomcrash
