#pragma once

#include "phantomcrash/battle.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/skeleton.hpp"
#include "phantomcrash/world_sound.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <random>

namespace phantomcrash {

// One battle sound as the two banks have it: the players' robots play from
// battle_player (bank 3), the computer's from battle (bank 1) or
// battle_old (bank 2), the robot's +8 telling them apart. Either may be
// empty: that side plays nothing.
struct SoundPair {
  std::string player;
  std::string computer;
  bool loop{};            // repeat the authored sequence until its owner stops it
  int trigger_policy{-1}; // -1 legacy, 0 shot, 1 held, 2 shared pair, 3 sustained beam
  bool requires_free_arm_action{};

  [[nodiscard]] const std::string &pick(bool piloted) const noexcept {
    return piloted ? player : computer;
  }
};

// A weapon's sound, chosen by the first rule it matches: its record's
// fire sound handler (+0x21, table 0x2537c0), its mount, its model (+8),
// its HUD kind (+0x1c) and its flight (+0x1d). A rule's -1 or empty list
// matches any.
struct WeaponSoundRule {
  int style{-1};
  int mount{-1}; // 0 an arm, 1 a shoulder
  std::vector<int> models;
  int hud_kind{-1};
  int flight{-1};
  SoundPair sound;
  struct FireCycle {
    int group{-1}; // recovered base cue; shared by all matching mounts on one robot
    bool start_together{};
    SoundPair secondary;
    SoundPair ending;
  } cycle;
  int on_robot{-1};
  struct Variant {
    std::string sound;
    double weight{1};
  };
  std::vector<Variant> variants;

  [[nodiscard]] bool matches(const WeaponTuning &weapon, int slot) const;
};

// data/audio/battle_sounds.json. The robots' own sounds (0x093637 and the
// functions it calls, docs/BATTLE_SOUNDS.md), the weapons' and the
// distance they carry.
struct BattleSoundTable {
  SoundFalloff falloff;
  SoundPair footstep; // 0x093cf0, from the walk's steps
  SoundPair landing;  // 0x093df2
  // 0x094d86: landing on these surfaces plays its own sound instead, the
  // first that matches (surface flags, stage_collision.hpp).
  struct SurfaceSound {
    unsigned flags{};
    std::string sound;
  };
  std::vector<SurfaceSound> landing_surfaces;
  SoundPair down_boost;          // 0x0905af, player bank 3 entry 20
  SoundPair jump;                // 0x08ea5b, 0x08d7fa
  SoundPair boost_start;         // 0x0945c1: a dash starts ...
  SoundPair boost;               // ... and this loops, following it, until it ends
  SoundPair dodge;               // 0x094a8d, motions 13 and 14
  SoundPair air_dodge;           // motions 15 and 16
  SoundPair shot_down;           // 0x07675e
  SoundPair low_armour;          // 0x093637: repeats while the armour is low
  float footstep_seconds{0.5F};  // without contact nodes: between steps at full walk (port value)
  float low_armour_share{0.25F}; // below this share of the armour (0x199fd4)
  float low_armour_seconds{32.0F / 30.0F}; // 0x20 battle ticks between alarms
  float beam_restart_seconds{9.0F};        // 0x09b9fe refreshes a held beam's long clip
  std::vector<WeaponSoundRule> fire;       // 0x099c02
  std::vector<WeaponSoundRule> impact;     // 0x098eda, 0x047ed9
  SoundPair fire_fallback;                 // a weapon without a record
  // A piloted robot's own (battle_sounds_pilot.cpp, the rest of 0x093637).
  // 0x0949a0: the upper body's servo starts while the twist stick pushes
  // past twist_push and the twist is inside twist_limit_degrees, and stops
  // once it is not.
  std::string twist_start;
  std::string twist_stop;
  float twist_push{0.1F};           // 0x17ac38
  float twist_limit_degrees{45.0F}; // 0x17d0e0
  std::string camouflage_on;        // 0x093c5d: state 1, turning on
  std::string camouflage_off;       // state 5, turning off
  std::string camouflage_break;     // state 7, a hit broke it: played twice
  std::string arm_lost;             // an arm weapon knocked off (+0x8c/+0x90 state 4) ...
  float arm_lost_seconds{5.0F};     // ... repeats this often (DAT_ac73b8 * 5); 0: once an arm
  std::string locked_on;            // loops while another robot locks on (+0x8a2, 0x051045)
  std::string carrier_engine;       // the supply carrier's loop (0x066940, stopped by 0x06828e)
};

[[nodiscard]] BattleSoundTable parse_battle_sounds(const json::JsonValue &value);

// Every file the table names, once each.
[[nodiscard]] std::vector<std::string> sound_files(const BattleSoundTable &table);

// A robot's landing sound: by the surface it lands on (0x094d86), else its own.
[[nodiscard]] const std::string &landing_sound(const BattleSoundTable &table,
                                               const Combatant &robot);

// The first rule's sound for a weapon on `slot`, or nullptr.
[[nodiscard]] const SoundPair *weapon_sound(const std::vector<WeaponSoundRule> &rules,
                                            const WeaponTuning &weapon, int slot,
                                            std::optional<bool> on_robot = std::nullopt);

// A sound for the runner, with where it is in the world.
struct PlacedCue {
  SoundCue cue;
  Vec3 at;
};

// Places each cue as heard by `listener`, or whole without one (the
// split screen's).
[[nodiscard]] std::vector<SoundCue>
place_cues(const SoundFalloff &falloff, const SoundListener *listener, std::vector<PlacedCue> cues);

// Watches a battle tick by tick and says what it sounds like: every
// robot's steps, jumps, landings, dashes and dodges, its weapons and their
// hits, its end, and a player's low-armour alarm.
class BattleSounds final {
public:
  using PlaybackQuery = std::function<bool(std::uint64_t)>;
  explicit BattleSounds(BattleSoundTable table, PlaybackQuery playing = {})
      : table_(std::move(table)), playing_(std::move(playing)) {}

  // After each Battle::tick.
  void observe(const Battle &battle, float seconds);
  [[nodiscard]] std::vector<PlacedCue> take() {
    return std::exchange(cues_, {});
  }
  [[nodiscard]] const BattleSoundTable &table() const noexcept {
    return table_;
  }

private:
  struct Watch {
    MoveMode mode{MoveMode::stand};
    bool grounded{true};
    bool alive{true};
    bool boosting{};
    bool down_boosting{};
    float step_left{};
    std::vector<PoseContact> contacts; // the feet's keys last tick
    bool stepped{};
    float alarm_left{};
    std::array<bool, weapon_slots> beams{};
    std::array<float, weapon_slots> beam_left{};
    struct Sustained {
      const WeaponTuning *weapon{};
      bool submitted{};
    };
    std::array<Sustained, weapon_slots> sustained;
    struct HeldImpact {
      const WeaponTuning *weapon{};
      std::string sound;
      Vec3 at;
    };
    std::array<HeldImpact, weapon_slots> held_impacts;
    struct FirePair {
      const WeaponSoundRule *rule{};
      std::array<bool, 2> submitted{};
    };
    std::map<int, FirePair> fire_pairs;
    bool twisting{};
    bool camouflaged{};
    bool locked{};
    float arm_alarm_left{};
    int arms_lost{};
  };

  void play(const std::string &name, Vec3 at, AudioBus bus = AudioBus::robots);
  void observe_robot(std::size_t index, const Combatant &robot, float seconds);
  // Whether the robot has contact nodes; sets watch.stepped when one
  // crossed a key since the last tick.
  static bool stepped(const Combatant &robot, Watch &watch);
  void observe_boost(std::size_t index, const Combatant &robot, Watch &watch);
  void observe_events(const Battle &battle);
  void observe_beams(const Battle &battle, float seconds);
  void observe_sustained(const Battle &battle);
  void observe_fire_pairs(const Battle &battle);
  void held_impact(const BattleEvent &event, const std::string &name);
  void observe_pilot(const Battle &battle, std::size_t index, float seconds);
  void observe_carrier(const Battle &battle);
  void loop(bool on, bool was, const std::string &name, std::uint64_t key, Vec3 at, AudioBus bus);
  [[nodiscard]] static std::uint64_t boost_key(std::size_t index);

  BattleSoundTable table_;
  PlaybackQuery playing_;
  std::vector<Watch> watch_;
  std::vector<PlacedCue> cues_;
  bool carrier_sounding_{};
  std::mt19937 variant_random_{std::random_device{}()};
};

} // namespace phantomcrash
