#pragma once

#include "phantomcrash/battle_hud_style.hpp"
#include "phantomcrash/battle_settings.hpp"
#include "phantomcrash/battle_weapons.hpp"
#include "phantomcrash/enemy_path.hpp"
#include "phantomcrash/robot_ai.hpp"
#include "phantomcrash/robot_mover.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/shown_armour.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {
class RobotCombat;

// The four weapon mounts, in the order the fire buttons use them: right
// trigger, left trigger, B, Y (Pepper's attack lesson, states 0x24-0x27).
enum class WeaponSlot { right_arm, left_arm, right_shoulder, left_shoulder };
inline constexpr int weapon_slots = 4;

struct Combatant {
  std::shared_ptr<RobotCombat> pose;
  bool pose_updated{};
  std::string name;
  std::string tag;       // the marker's name tag (its pilot's); empty for none
  bool leader{};         // its team's leader: the marker shows the leader icon
  int player_number{-1}; // versus mode's 1P-4P label (0-3), shown without a tag
  int team{};
  // HUD palette override; -1 follows the combat team (training/versus).
  // RUMBLING uses roster status independently of its free-for-all allegiance.
  int marker_colour{-1};
  std::array<bool, weapon_slots> firing{}; // accepted held triggers, for sustained weapon audio
  std::array<bool, weapon_slots> trigger_held{}; // control input, independent of cooldown/bursts
  bool piloted{}; // a player controls it (robot +8 not -1): the first robot added
  bool rival{};   // on the side against the players (loadout +0x6c 2): the drones
  bool boss{};    // a boss (loadout +0x70 1): enters with its own scene (0x082cff)
  RobotMover mover;
  float radius{2.0F};              // the collision spheres' (0x22f424 per leg type)
  unsigned stage_contact{};        // stage_contact flags from the last tick
  int floor_surface{-1};           // the stage surface it stands over (-1: none, or a prop)
  std::optional<Vec3> last_ground; // where it last stood with stage ground under it
  float fence_contact_left{};      // delay until another force-field contact pulse
  float hp{100.0F};
  float max_hp{100.0F};
  bool alive{true};
  bool dying{}; // no longer targetable, but still playing the death motion
  float death_seconds{};
  bool drone{};  // walks the enemy path and respawns
  bool frozen{}; // stands still (the lessons freeze the drones, 0x091ff4)
  float respawn_left{};
  bool auto_respawn{true}; // a drone shot down comes back by itself
  int shot_by{-1};         // who did most damage to it when it was last shot down
  std::array<const WeaponTuning *, weapon_slots> weapons{};
  std::array<float, weapon_slots> cooldown{};
  std::array<Magazine, weapon_slots> magazines{};
  // The robot each weapon would lock onto, how long it has been held and
  // the lock itself: missiles and grenades lock once held lock_seconds
  // (the AI chip's lock time), guns and melee weapons at once.
  std::array<int, weapon_slots> lock_candidates{-1, -1, -1, -1};
  std::array<float, weapon_slots> lock_held{};
  std::array<int, weapon_slots> locks{-1, -1, -1, -1};
  float lock_seconds{};
  SightBoxes sight; // its chip's boxes, for locking without a drawn reticle
  // The mount whose melee weapon is striking with its blade armed, or -1
  // (0x09c337; the attacker's +0x25a5).
  int strike_slot{-1};
  bool source_free_arm_action{}; // S.L.A.I. actor +0x19c; completed-lock lunges do not set it
  // A homing missile's rounds leave one at a time (0x09b4ed): how many
  // are still to leave from each mount, and when the next one does.
  std::array<int, weapon_slots> queued_rounds{};
  std::array<float, weapon_slots> queue_left{};
  std::array<bool, weapon_slots> lost{}; // weapons a critical hit knocked off
  CombatStats combat;                    // its chip's and legs' (robot_parts.hpp)
  float invulnerable{};                  // seconds no hit hurts it (after a dodge)
  float shield{};                        // seconds its entry shield still lasts (+0x27d4)
  float shield_glow{};                   // the shield's glow, 0..peak (+0xac)
  Vec3 push;                             // knockback still carrying it, units a second
  std::vector<float> damage_from;        // by attacker, since it was last whole
  int kills{};
  float hit_flash{};        // seconds of white left (hit_flash_seconds)
  ShownArmour shown_armour; // the gauges' armour (+0x50)
  bool camouflaged{};
  float camouflage{1.0F};   // gauge, 0..1
  float camouflage_sweep{}; // 0 seen .. 1 fully camouflaged, easing after `camouflaged`
  bool camouflage_broken{}; // a hit turned it off (0x075444): it fades out faster
  float ammo_share{1.0F};   // robot +0x110 (0x08248b), as of the last tick
  float idle_seconds{};     // standing untouched (robot +0x251c)
  PathFollower path;
  RobotAiState ai;

  explicit Combatant(const MovementTuning &tuning) : mover(tuning) {}
  [[nodiscard]] Vec3 centre(float height) const {
    return mover.position() + Vec3{0.0F, height * 0.5F, 0.0F};
  }
  // Where the robot aims from: one leg radius above its feet, the middle
  // of its lower collision sphere (robot +0xb0 plus +0x14, 0x098bb7).
  [[nodiscard]] Vec3 aim_origin() const {
    return mover.position() + Vec3{0.0F, radius, 0.0F};
  }
};

} // namespace phantomcrash
