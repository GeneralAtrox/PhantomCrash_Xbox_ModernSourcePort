#pragma once

// What the headless AI simulator records (a research tool): each computer
// robot's life in the battle from the moment it comes in until it is shot
// down, leaves or the run ends, measured from the outside of the AI
// (Combatant::ai, the battle's events) without changing what it does.

#include "phantomcrash/battle.hpp"

#include <array>
#include <deque>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace phantomcrash::ai_sim {

class SimArena;

// A mount that can fire now (or once its magazine recharges).
[[nodiscard]] bool usable(const Combatant &robot, std::size_t slot);
// `other` is someone `self` may fight.
[[nodiscard]] bool rival(const Combatant &self, const Combatant &other);

inline constexpr int ai_modes = 7; // AiMode's values

struct ProbeTuning {
  float sample_seconds{0.5F};     // movement samples
  float window_seconds{3.0F};     // a stuck robot has moved less than ...
  float stuck_distance{2.0F};     // ... this far over the window
  float wants_move{0.05F};        // its stick past this asks to travel
  float sight_seconds{0.25F};     // the opportunity check's interval
  float idle_after_fire{1.0F};    // with a shot this recent it is not holding fire
  float acquire_range{100.0F};    // robot_ai.json acquire_range
  float long_stuck_seconds{5.0F}; // an episode this long is reported as long
  // Behaviour (ai_sim_probe_behaviour.cpp).
  float heavy_share{0.2F};  // a heavy hit takes this share of the armour ...
  float heavy_window{1.0F}; // ... within this long
  float dodge_window{1.5F}; // a dodge this soon after a heavy hit or a lock answers it
  float turn_window{5.0F};  // a robot shot turns on its attacker this soon or not at all
  float hunt_window{30.0F}; // after a kill it finds someone new this soon or not at all
  float moving_speed{1.0F}; // faster than this it is moving
  int piled_on{3};          // this many robots on one target is a pile-on
};

// How a robot fights: its weapons, camouflage, dodges and answers to being
// shot, locked on to or left without a target.
struct BehaviourStats {
  std::array<int, weapon_slots> slot_shots{};       // fired, by mount
  std::array<double, weapon_slots> slot_ready{};    // seconds the mount could fire
  std::array<double, weapon_slots> slot_in_reach{}; // seconds its target was in the mount's reach
  double camouflaged{};
  int camouflage_uses{}, camouflage_threat_uses{};
  int dodges_threat{}, dodges_other{};
  int heavy_hits{}, heavy_hit_dodges{};
  int locked_on{}, locked_on_dodges{};
  // Shot by a rival other than its target, holding no target (free) or
  // another (busy); turned: it took the attacker as its target in time.
  int shot_free{}, turned_free{}, shot_busy{}, turned_busy{};
  double turn_seconds{};
  int kills_after{}, hunted{};
  double hunt_seconds{};
  double no_target{}, no_target_moving{};
};

struct LifeStats {
  int run{}, area{}, robot{}, entrant{-1}, ability{}, profile{};
  std::string name, tag;
  float radius{}, jump_rise{}; // its collision sphere and held jump
  std::array<std::string, weapon_slots> weapons;
  double start{}, end{};
  std::string ended{"survived"}; // destroyed, withdrawn or survived
  double alive{};
  std::array<double, ai_modes> mode{};
  double target{}, visible{}, in_reach{}, request{}, locked{}, trigger{};
  double opportunity{}, opportunity_no_target{}, opportunity_holding{};
  double moving{}, stuck{}, still_fighting{}, still_idle{};
  int stuck_episodes{}, long_stuck_episodes{};
  double longest_stuck{};
  // Blocked: pushing the stick without getting anywhere. Standing to aim,
  // turning on the spot and lining up a jump do not count.
  double blocked{};
  int blocked_episodes{}, long_blocked_episodes{};
  double longest_blocked{};
  double distance_travelled{};
  int shots{}, hits{}, kills{}, assists{}, pickups{};
  double damage_dealt{}, damage_taken{};
  double first_seen{-1}, first_shot{-1}, first_hit{-1};
  std::vector<double> reactions;
  int acquisitions{}, unanswered{};
  unsigned transitions{}, detections{}, dodges{}, recoveries{};
  double nearest_sum{};
  int nearest_count{};
  double no_enemy_near{};
  double player_target{}, player_damage{};
  BehaviourStats behaviour;
};

// What a robot was moving for when it stuck (inferred from its AI state).
enum class StuckPurpose { none, route, vantage, approach, search };

struct StuckPoint {
  int run{}, area{}, robot{}, entrant{};
  double t{}, seconds{};
  Vec3 at;
  int mode{};
  bool nav{};
  StuckPurpose purpose{};
  bool target{};         // it had a target
  Vec3 goal;             // its route's goal
  Vec3 waypoint;         // the waypoint it was making for
  int link{-1};          // that waypoint's NavLink (-1: none)
  float crowd{-1};       // the nearest other robot (-1: none alive)
  unsigned recoveries{}; // route recoveries during the episode
};

struct RunStats {
  int run{}, area{}, contest{-1}, day{};
  std::string stage;
  bool player{};
  double seconds{}, wall_seconds{};
  int lives{}, kills{}, destroyed{}, withdrawn{}, shots{}, hits{};
  double damage{};
  double first_kill{-1}, first_shot{-1};
  double alive_robot_seconds{};
  double player_damage_taken{}, player_targeted{}, player_first_targeted{-1};
  int player_shots_at{};
  // Robots holding the player (or a computer robot) as their target, summed
  // over time; seconds anyone held it; seconds a pile-on held it.
  double player_attackers{}, player_attacked{}, player_piled{}, player_alive{};
  double rival_attackers{}, rival_attacked{}, rival_piled{}, rival_alive{};
};

class Probe {
public:
  Probe(int run, SimArena &arena, ProbeTuning tuning, std::ostream *trace);

  // After each battle tick; `t` is the battle clock.
  void observe(double t, float seconds);
  // Ends the lives still running; the stats are complete afterwards.
  void finish(double t, double wall_seconds);

  [[nodiscard]] const std::vector<LifeStats> &lives() const noexcept {
    return lives_;
  }
  [[nodiscard]] const std::vector<StuckPoint> &stuck_points() const noexcept {
    return stuck_points_;
  }
  [[nodiscard]] const RunStats &run() const noexcept {
    return run_;
  }

private:
  struct Sample {
    double t{};
    Vec3 at;
    bool wants{};
    bool pushes{}; // the stick itself asks to travel (not just a route)
  };
  struct Track {
    std::optional<LifeStats> life;
    bool alive{};
    float hp{};
    std::vector<float> damage_from;
    std::deque<Sample> samples;
    double next_sample{}, next_sight{}, next_trace{};
    double last_fired{-1e9};
    double acquired_at{-1};
    bool was_visible{};
    int last_target{-1};
    double stuck_since{-1};
    double blocked_since{-1}; // pushing the stick without getting anywhere
    Vec3 last_at;
    std::optional<StuckPoint> episode;
    // Behaviour.
    std::deque<std::pair<double, float>> recent_damage;
    std::vector<float> blame_seen;
    double heavy_until{-1}, locked_until{-1}, turn_since{-1}, hunt_since{-1};
    int turn_attacker{-1}, kills_seen{};
    bool turn_busy{}, was_locked{}, was_camouflaged{};
    Vec3 behaviour_at;
  };

  void begin_life(int index, double t);
  void close_blocked(int index, double t);
  void end_life(int index, double t, const std::string &how);
  void read_events(double t, std::vector<bool> &destroyed);
  void read_damage(const std::vector<bool> &destroyed);
  void watch_ai(int index, double t, float seconds);
  void watch_sight(int index, double t);
  void watch_motion(int index, double t);
  void watch_behaviour(int index, double t, float seconds);
  void watch_crowding(float seconds);
  void close_episode(int index, double t);
  [[nodiscard]] StuckPoint open_episode(int index, double t) const;
  void write_trace(int index, double t);
  [[nodiscard]] float reach(const Combatant &robot, const Combatant *target) const;

  int run_index_{};
  SimArena &arena_;
  ProbeTuning tuning_;
  std::ostream *trace_{};
  std::vector<Track> tracks_;
  std::vector<LifeStats> lives_;
  std::vector<StuckPoint> stuck_points_;
  RunStats run_;
};

} // namespace phantomcrash::ai_sim
