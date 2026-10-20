#pragma once

#include "phantomcrash/battle.hpp"
#include "phantomcrash/chase_camera.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/motion.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

// Robot states with a scripted scene (the state table at 0x242d38): a
// player's robot enters the battle in one of states 1-4, which 0x088ffa
// picks at random; a computer robot enters in state 1, or in state 5 when a
// boss makes its entrance; a player's robot leaves in state 6. The numbers
// are the original's; the names are researcher-assigned.
namespace scene_state {
inline constexpr int none = 0;
inline constexpr int entrance_first = 1;
inline constexpr int entrance_last = 4;
inline constexpr int computer = 1;
inline constexpr int boss = 5;
inline constexpr int exit = 6;
} // namespace scene_state

// The scenes' frames are 60 Hz frames: a battle tick at 30 Hz takes two.
inline constexpr float scene_frames_per_second = 60.0F;

// A scene's camera (0x0ae01d): its channels sampled at the scene's frame.
// The eye's and the target's z channels are stored negated.
struct SceneCamera {
  MotionChannel roll; // radians about the line of sight
  MotionChannel fov;  // vertical field of view, degrees
  std::array<MotionChannel, 3> eye;
  std::array<MotionChannel, 3> target;
};

struct SceneTrack {
  float frames{};
  bool relative{}; // the camera stands where the scene starts, turned by its heading
  SceneCamera camera;
  // The robot's walk in its own frame (a motion track's translation), or none.
  std::optional<std::array<MotionChannel, 3>> walk;
};

// Where robots enter a stage and where they leave it (0x201598).
struct Gate {
  Vec3 position;
  float heading{};
};

struct BattleSceneTables {
  std::map<int, SceneTrack> states;
  std::map<int, std::vector<Gate>> gates; // by stage
};

// The converter's tables/battle_scenes.json. Throws std::runtime_error when
// a value is missing or wrong.
[[nodiscard]] BattleSceneTables parse_battle_scenes(const json::JsonValue &document);

// The scenes' rules and timing (data/world/training.json "scenes"); the
// defaults retain the requested faster NPC entry without its standing pause.
struct SceneTuning {
  std::string tables{"tables/battle_scenes.json"};
  int stage{6};                        // the gate table's stage (the training ground)
  int gate{0};                         // the player enters at robot index % gates (0x0825df)
  bool entrance{true};                 // FreePlay enters in state 1-4 (0x080778, save +0x10f10 3)
  bool exit{true};                     // walking back into a gate leaves the battle (0x08224d)
  bool computer_entrance{true};        // computer robots walk out of their gates too (0x082552)
  float computer_entrance_speed{2.0F}; // NPC scene playback, including the door delay
  bool computer_entrance_skip_stand{true}; // hand control to AI when the walk ends
  bool computer_exit{true};                // ... and leave by walking back into one (0x08224d)
  bool rider_entrance{true};               // VS: each piloted robot walks out in state 1 (0x080778)
  // Battle modes 0 and 2, not the lessons' mode 4: the entrances raise
  // chatter, the player's plays the music cue, and computer robots wait
  // for the other robots' scenes (0x09199e).
  bool regular_battle{};
  // The story's ranker contests (mission types 0x16 and 0x17, 0x082c54),
  // set by the match: no robot leaves by walking into a gate, it only signs
  // off (0x08224d); the ranker enters at ranker_gate (save +0x6120,
  // 0x0825df) and waits all of entry_delay before it walks out (0x088ffa).
  bool ranker{};
  int ranker_gate{-1};
  float ready_frame{90.0F};        // READY shows from here ... (0x19ef0c)
  float go_frame{30.0F};           // ... and GO! from here to the end (0x17ccfc)
  float stand_frame{180.0F};       // the walk ends and the robot stands (0x19f8d8)
  float door_frame{480.0F};        // the gate's door opens (0x199fe4) ...
  float close_frame{30.0F};        // ... and the exit shuts it behind the robot (0x17ccfc)
  float play_in_frame{360.0F};     // the entrance's music cue: 6 s before it ends (0x19f898)
  float music_gap_seconds{3.0F};   // silence before the next battle song (0x09e24c)
  float entry_delay{120.0F};       // a computer robot waits this long at most for a busy door
  float blend_frames{20.0F};       // the camera eases into the chase view (0x199f7c)
  float letterbox_frames{30.0F};   // the bars slide over this long (0x049080)
  float gate_radius{5.97F};        // a gate holds robots this close across ... (0x19f878)
  float gate_height{10.5F};        // ... and up to this far above it (0x19f87c)
  float exit_facing{0.1F};         // radians: the robot faces the gate within this (0x17ac38)
  float exit_turn{0.10471976F};    // radians a battle tick while it turns (0x1b5b58)
  float exit_step{0.2F};           // units a battle tick it walks to the gate (0x19b584)
  float still_step{0.01F};         // less a battle tick holds the leg motion (0x199f78)
  float left_armour{0.25F};        // armour share that picks chatter 0x1c over 9 (0x199fd4)
  float gone_shield_frames{30.0F}; // a computer robot in its gate, the exit walked (0x08a496)
  // A boss a player shoots down holds the battle this long (0x089861:
  // robot +0x2750), the First Ranker longer, while the camera watches it
  // from no farther than defeat_watch (0x089bb9 calls 0x07e538 with 10).
  float defeat_frames{180.0F};
  float first_ranker_defeat_frames{240.0F};
  float defeat_watch{10.0F};
  std::string ready_sound{"battle_ready"};     // sys bank entry 0 (0x013085)
  std::string go_sound{"battle_go"};           // sys bank entry 1
  std::string play_in_music{"battle_play_in"}; // track 16, 9PLAYIN
  std::string boss_music{"boss_start"};        // track 6, 13BossStart
  // The piloted robots take no hits while a boss's entrance holds the
  // camera (port: their controls wait while the others fight on).
  bool boss_spares_players{true};
  DoorTuning doors;
};

[[nodiscard]] SceneTuning parse_scene_tuning(const json::JsonValue &value);

// Where a robot that walks out of `gate` in `state` stands once its walk
// is done (at stand_frame), on the gate's floor; the gate itself without
// the scene or its walk.
[[nodiscard]] Vec3 walked_out(const BattleSceneTables &tables, const SceneTuning &tuning,
                              const Gate &gate, int state = scene_state::computer);

// What the entrance shows in the middle of the screen.
enum class SceneCue { none, ready, go };

// The robots' scripted entrances and the player's exit. The player's
// entrance walks robot 0 out of its gate while the camera follows one of
// four shots, under letterbox bars, then READY and GO!; the exit starts
// when it walks back into a gate, turns it to the gate and walks it in, and
// ends the battle. Computer robots, including bosses, use the same walk at
// their configured speed and can skip its final standing phase. A computer robot
// that walks back into a gate leaves too, and
// once its door has shut it is out of the battle (a drone comes back as a
// shot-down one does). Robots are shielded while they walk in or out.
// Entrances open the gates' doors.
class BattleScene final {
  friend class StateInspector;

public:
  BattleScene(BattleSceneTables tables, SceneTuning tuning, std::uint32_t seed = 1);

  // Puts robot 0 at its gate and starts one of the entrance's states.
  // False when the tables have no gate or entrance for it.
  bool start_entrance(Battle &battle);
  // Puts computer robot `index` at its gate to walk out (0x082552); it
  // takes up the enemy path when it is out. False without a gate.
  bool start_robot_entrance(Battle &battle, int index);
  // Puts piloted robot `index` (a VS rider) at gate `gate` to walk out in
  // state 1 (0x080778, 0x082552) with the computer robots' walk; its
  // controls wait until it stands, and it takes up no path. False without
  // the gate or the walk.
  bool start_rider_entrance(Battle &battle, int index, int gate);
  [[nodiscard]] int robot_gate(int index) const;
  // Before Battle::tick: walks the robots through their scenes.
  void prepare(Battle &battle, float seconds);
  // After Battle::tick: the gates, which start the exit, respawned drones,
  // which enter again, and the chatter.
  void observe(Battle &battle);

  // The player's robot's state.
  [[nodiscard]] int state() const noexcept {
    return state_;
  }
  // A computer robot's state (scene_state::none when it is not entering).
  [[nodiscard]] int robot_state(int index) const;
  // The player's robot is in a scene: its controls and the HUD wait (0x04eaa3).
  [[nodiscard]] bool active() const noexcept {
    return state_ != scene_state::none;
  }
  // A boss is making its entrance: the camera shows it.
  [[nodiscard]] bool cinematic() const noexcept {
    return cinematic_ >= 0;
  }
  // The player's scene or a boss's: the controls, the HUD and the camera wait.
  [[nodiscard]] bool scripted() const noexcept {
    return active() || cinematic() || defeat_ >= 0;
  }
  // A boss shot down by a player's robot (0x07675e puts it in state 0x13,
  // battle_scene_defeat.cpp): the bars slide in and the battle waits
  // `frames` and for its line to end (set_defeat_line). A ranker's ends the
  // battle (`ends`, 0x089d1b); another boss's gives the battle back.
  void start_defeat(int robot, float frames, bool ends);
  void set_defeat_line(bool speaking) noexcept {
    defeat_speaking_ = speaking;
  }
  // The boss whose defeat the camera watches, or -1.
  [[nodiscard]] int defeat_robot() const noexcept {
    return defeat_;
  }
  // A defeat that ends the battle has played out.
  [[nodiscard]] bool defeat_over() const noexcept {
    return defeat_over_;
  }
  [[nodiscard]] bool defeat_holds() const noexcept {
    return defeat_ >= 0 && !defeat_over_;
  }
  // The scene on screen now: a boss's entrance, the player's entrance or
  // exit (robot -1), or else a VS rider's walk out of its gate; its state
  // and how far it has run in 60 Hz frames of its track's.
  struct Playback {
    int state{};
    int robot{-1};
    float frame{};
    float total{};
  };
  [[nodiscard]] std::optional<Playback> playback() const;
  // The exit has ended: the battle is over.
  [[nodiscard]] bool finished() const noexcept {
    return finished_;
  }
  // The scene's shot, eased into `chase` at the entrance's end; none
  // outside a scene.
  [[nodiscard]] std::optional<CameraShot> shot(const CameraShot &chase) const;
  // How far the letterbox bars are in, 0..1.
  [[nodiscard]] float letterbox() const noexcept;
  [[nodiscard]] SceneCue cue() const noexcept;
  [[nodiscard]] std::vector<std::string> take_sounds() {
    return std::exchange(sounds_, {});
  }

  // A music track to play once, from music.json (the entrances' cues).
  [[nodiscard]] std::optional<std::string> take_music() {
    return std::exchange(music_, std::nullopt);
  }

private:
  // A computer robot's entrance (robot +0x2774 state, +0x277c delay).
  struct RobotEntrance {
    int robot{};
    int state{scene_state::none};
    bool walking{}; // done waiting at the gate
    float delay{};  // frames it still waits for a busy door
    float left{};   // frames still to play
    int gate{-1};
    Gate origin;
    int gate_flag{}; // robot +0x10, as the player's gate_flag_
    float heading{}; // the exit's, toward the gate
    bool rider{};    // a piloted VS robot (start_rider_entrance)
  };

  [[nodiscard]] const SceneTrack *track(int state) const;
  [[nodiscard]] const std::vector<Gate> *gates() const;
  [[nodiscard]] int gate_at(const Combatant &robot) const;
  [[nodiscard]] std::optional<CameraShot> scene_shot(int state, const Gate &origin,
                                                     float left) const;
  void walk_entrance(Battle &battle, float seconds, float step);
  void walk_exit(Battle &battle, float seconds, float step);
  void start_exit(Battle &battle, int gate);
  // Robot `index` turns to `heading`, then walks to `gate` (0x08a161).
  void exit_step(Battle &battle, int index, float heading, Vec3 gate, float seconds,
                 float step) const;
  void sign_off(Battle &battle, int index) const;
  [[nodiscard]] float heading_to(const Combatant &robot, Vec3 gate) const;
  // Robot `index` walks `state`'s walk in `origin`'s frame, from `before`
  // to `left` frames still to play; it stands from stand_frame on.
  void walk_step(Battle &battle, int index, int state, const Gate &origin, float before, float left,
                 float seconds) const;
  void open_door(Battle &battle, int gate, float before, float left) const;
  // battle_scene_robots.cpp
  [[nodiscard]] int next_robot_gate(const Battle &battle, int count);
  void walk_robot(Battle &battle, RobotEntrance &entry, float seconds, float step);
  [[nodiscard]] bool waits_for_others(const Battle &battle, const RobotEntrance &entry) const;
  void start_walking(Battle &battle, RobotEntrance &entry);
  void end_robot(Battle &battle, RobotEntrance &entry);
  void observe_robot_gates(Battle &battle);
  void start_robot_exit(Battle &battle, RobotEntrance &entry, int gate);
  void walk_robot_exit(Battle &battle, RobotEntrance &entry, float seconds, float step);
  void hold_still(Battle &battle, int index) const;
  // battle_scene_defeat.cpp
  void tick_defeat(float step);

  // Fixed once loaded: copies and rollback checkpoints share them.
  std::shared_ptr<const BattleSceneTables> tables_;
  SceneTuning tuning_;
  std::minstd_rand random_;
  int state_{scene_state::none};
  float left_{}; // frames still to play
  Gate origin_;  // where the scene started: its camera's frame
  int gate_{-1}; // its gate, whose door it opens and shuts
  float exit_heading_{};
  int gate_flag_{}; // 0x08224d's robot +0x10: 0 in a gate, 1 out of it, 2 back in
  std::vector<RobotEntrance> robots_;
  std::vector<int> remaining_gates_; // shuffled NPC round, consumed from the back
  int last_robot_gate_{-1};
  int cinematic_{-1};  // the robot whose boss scene holds the battle
  bool boss_played_{}; // global +0xef328: a battle's first boss only
  float letterbox_{};
  float letterbox_way_{};                  // +1 sliding in, -1 out
  std::vector<std::pair<int, int>> lines_; // chatter to raise: robot, event
  bool finished_{};
  int defeat_{-1};      // the boss in its defeat (state 0x13), or -1
  float defeat_left_{}; // its frames still to play (+0x2750)
  bool defeat_ends_{};  // a ranker's: the battle ends with it
  bool defeat_over_{};  // ... and it has
  bool defeat_speaking_{};
  std::vector<std::string> sounds_;
  std::optional<std::string> music_;
};

} // namespace phantomcrash
