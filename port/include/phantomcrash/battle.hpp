#pragma once

#include "phantomcrash/battle_events.hpp"
#include "phantomcrash/battle_hud_style.hpp"
#include "phantomcrash/battle_settings.hpp"
#include "phantomcrash/combatant.hpp"
#include "phantomcrash/entry_shield.hpp"
#include "phantomcrash/enemy_path.hpp"
#include "phantomcrash/robot_mover.hpp"
#include "phantomcrash/shown_armour.hpp"
#include "phantomcrash/stage_collision.hpp"
#include "phantomcrash/stage_doors.hpp"
#include "phantomcrash/stage_objects.hpp"
#include "phantomcrash/supply_drop.hpp"

#include <array>
#include <optional>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace phantomcrash {

// The active player's camera; opponents use their weapon's 3D aim cone.
// The fire control's reticle on the view, in normalised device
// coordinates (x right, y up): where it has tracked to (robot +0x108,
// +0x10c, 0x04f6a0) and the half sizes of its inner and outer boxes.
struct LockBoxes {
  std::array<float, 2> centre{};
  std::array<float, 2> inner_half{};
  std::array<float, 2> outer_half{};
};

struct TargetView {
  Mat4 view_projection;
  Vec3 eye;
  std::optional<LockBoxes> boxes; // none: the weapons' lock cones stand in
};

// The player's buttons for weapons this tick.
struct FireControl {
  std::array<bool, weapon_slots> fire{}; // held
  bool camouflage{};                     // pressed: toggles Optic Camouflage
};

// Tools > Cheats for the piloted robots (a port addition; off by default).
struct BattleCheats {
  bool never_die{};     // armour stops at 1
  bool infinite_ammo{}; // weapons stay full
};

// A battle on one stage: robot 0 is the player; the others are drones
// walking the stage's enemy path, respawning when destroyed.
class Battle final {
  friend class StateInspector;

public:
  Battle(BattleSettings settings, EnemyPath path, std::uint32_t seed = 1);
  // Robots point into the settings' weapons: no copies.
  Battle &operator=(const Battle &) = delete;

  // Local rollback checkpoints own mutable state and rebind internal pointers.
  // RobotLibrary assets must outlive the battle and its checkpoints.
  [[nodiscard]] std::shared_ptr<const Battle> checkpoint() const;
  void restore_checkpoint(const Battle &checkpoint);
  // Presentation caches must rebuild after rewind, even if object counts match.
  // This counter does not participate in gameplay or deterministic state hashes.
  [[nodiscard]] std::uint64_t restore_generation() const {
    return restore_generation_;
  }
  // Online startup only: do not let worker completion change AI on a local tick.
  void prepare_deterministic();

  // Adds a robot; `weapons` names entries of the settings' weapons (empty
  // for none). Returns its index.
  int add_robot(const std::string &name, int team, Vec3 position, float heading,
                const std::array<std::string, weapon_slots> &weapons, float hp);
  // Adds a drone at the start of one of the path's entry segments.
  int add_drone(const std::string &name, float hp);
  // Adds `tuning` to the settings' weapons (replacing one of its name) for
  // robots to carry; the reference stays valid.
  const WeaponTuning &add_weapon(WeaponTuning tuning);
  // Robot `index` carries these weapons (settings' names; empty for none),
  // their magazines full.
  void arm(int index, const std::array<std::string, weapon_slots> &weapons);
  // Robot `index`'s name, team and armour (whole).
  void set_identity(int index, const std::string &name, int team, float hp);
  // Whether drone `index` comes back by itself once shot down.
  void set_auto_respawn(int index, bool automatic);
  // Drone `index` comes in whole at one of the path's entries.
  void bring_in(int index);
  // External arrivals survive until the scene handles them, across battle ticks.
  [[nodiscard]] std::vector<int> take_arrivals();
  // Robot `index` is out of the battle until brought back (not shot down).
  void take_out(int index);
  // Robot `index`, to restore or change its state.
  [[nodiscard]] Combatant &robot(int index) {
    return robots_.at(static_cast<std::size_t>(index));
  }

  void tick(const RobotControl &player, const FireControl &fire, float seconds);
  void select_player(int index) {
    player_index_ = index;
  }
  [[nodiscard]] int player_index() const {
    return player_index_;
  }
  void set_cooperative(bool value) {
    cooperative_ = value;
  }
  [[nodiscard]] bool cooperative() const {
    return cooperative_;
  }
  // VS: every piloted robot has its own controls (set_player_control) and,
  // unlike co-op, they damage each other.
  void set_versus(bool value) {
    versus_ = value;
  }
  [[nodiscard]] bool versus() const {
    return versus_;
  }
  void set_player_control(int index, RobotControl control, FireControl fire) {
    controls_[index] = {control, fire};
  }

  void set_drones_active(bool active);
  // Robot `index` moves with its own tuning (its leg part's stride speeds).
  void set_movement(int index, const MovementTuning &tuning);
  // Robot `index`'s collision radius (its leg part's).
  void set_radius(int index, float radius);
  // Robot `index`'s marker name tag.
  void set_tag(int index, std::string tag);
  // Robot `index`'s marker extras: the leader icon and a versus player number.
  void set_marker(int index, bool leader, int player_number);
  // Robot `index` is a boss (loadout +0x70 1): it enters with its own scene.
  void set_boss(int index, bool boss);
  // What robot `index`'s chip and legs give it in a fight.
  void set_combat(int index, const CombatStats &combat);
  // The stage's walls and floor in place of the settings' box arena.
  void set_stage(StageCollision stage);
  [[nodiscard]] const StageCollision &stage() const noexcept {
    return stage_;
  }
  // The gates' doors: robots push against them and open them (stage_doors.hpp).
  void set_doors(StageDoors doors);
  [[nodiscard]] RobotWrecks &wrecks() {
    return wrecks_;
  }
  [[nodiscard]] const RobotWrecks &wrecks() const {
    return wrecks_;
  }

  void set_objects(StageObjects objects) {
    objects_ = std::move(objects);
  }
  [[nodiscard]] StageObjects *objects() {
    return objects_ ? &*objects_ : nullptr;
  }
  [[nodiscard]] const StageObjects *objects() const {
    return objects_ ? &*objects_ : nullptr;
  }
  // The stage's supply drops (supply_drop.hpp); none without routes.
  void set_supply(SupplySettings settings);
  [[nodiscard]] const SupplyDrop *supply() const noexcept {
    return supply_ ? &*supply_ : nullptr;
  }
  [[nodiscard]] StageDoors *doors() noexcept {
    return doors_ ? &*doors_ : nullptr;
  }
  [[nodiscard]] const StageDoors *doors() const noexcept {
    return doors_ ? &*doors_ : nullptr;
  }
  // Weapons the player may fire (bit per WeaponSlot); the rest stay silent.
  void set_allowed_weapons(unsigned mask) noexcept {
    allowed_weapons_ = mask;
  }
  void set_cheats(const BattleCheats cheats) noexcept {
    cheats_ = cheats;
  }
  // Robot `index` locks missiles and grenades after holding a target this long.
  void set_lock_seconds(int index, float seconds);
  void set_sight(int index, const SightBoxes &boxes);
  // Puts robot `index` `distance` ahead of the player, facing it (the
  // Optic Camouflage lesson, 0x08d09e).
  void place_facing_player(int index, float distance);
  // Stands robot `index` at `position` facing `heading`.
  void place(int index, Vec3 position, float heading);
  // Robot `index` walks a script next tick instead of following its
  // controls, until released (battle_scene.hpp).
  void script(int index, const ScriptedStep &step);
  void release(int index);
  // Robot `index` leaves the battle by a gate, not shot down; a drone
  // comes back as a shot-down one does (0x08a496, 0x047c9e).
  void withdraw(int index);
  // Drone `index` takes up the enemy path where it stands (after its entrance).
  void join_path(int index);
  // Robot `index` is shielded for `seconds` (entry_shield.hpp): the
  // settings' entry shield when none are given.
  void raise_shield(int index);
  void raise_shield(int index, float seconds);
  // Raises chatter `event` for robot `index` among this tick's events.
  void tell(int index, int event);

  [[nodiscard]] const std::vector<Combatant> &robots() const noexcept {
    return robots_;
  }
  [[nodiscard]] const Combatant &player() const {
    return robots_.at(static_cast<std::size_t>(player_index_));
  }
  [[nodiscard]] const Armory &armory() const noexcept {
    return armory_;
  }
  [[nodiscard]] const BattleSettings &settings() const noexcept {
    return settings_;
  }
  // The player's current lock for `slot`'s weapon, or -1.
  [[nodiscard]] int player_lock(WeaponSlot slot) const;
  // The robot `slot`'s weapon is locking onto (or has locked), or -1, and
  // for how many seconds.
  [[nodiscard]] int player_lock_candidate(WeaponSlot slot) const;
  [[nodiscard]] float player_lock_held(WeaponSlot slot) const;
  // What happened during the last tick.
  [[nodiscard]] const std::vector<BattleEvent> &events() const noexcept {
    return events_;
  }
  // Robots destroyed since the battle began.
  [[nodiscard]] int destroyed() const noexcept {
    return destroyed_;
  }
  // Where a robot's shots leave from and where it aims (a unit vector).
  [[nodiscard]] Vec3 muzzle(const Combatant &robot, WeaponSlot slot) const;
  [[nodiscard]] Vec3 aim(const Combatant &robot) const;
  void set_player_target_view(std::optional<TargetView> view) {
    target_views_[player_index_] = std::move(view);
  }
  [[nodiscard]] std::optional<TargetView> player_target_view() const {
    const auto found = target_views_.find(player_index_);
    return found == target_views_.end() ? std::nullopt : found->second;
  }
  // Online simulation owns poses even when a newly arrived actor is not drawn.
  void own_simulation_poses() {
    simulation_poses_ = true;
  }
  [[nodiscard]] bool simulation_poses() const {
    return simulation_poses_;
  }
  // Shared by the tracking reticle and weapon acquisition/lock maintenance.
  [[nodiscard]] bool target_visible(const Combatant &observer, const Combatant &target) const;

private:
  Battle(const Battle &) = default;
  Battle &operator=(Battle &&) = default;
  [[nodiscard]] std::unique_ptr<Battle> clone_state() const;
  [[nodiscard]] bool clear_target_sight(Vec3 from, Vec3 to) const;
  [[nodiscard]] std::vector<Target> targets() const;
  // Where the robot locks: the drawn reticle's boxes, or boxes around its
  // sight seen through its camera, or through its own eyes (sight_view).
  [[nodiscard]] std::optional<TargetView> sight_view(const Combatant &robot) const;
  // The robot to lock `weapon` on `slot` onto: the nearest within its lock
  // range inside the inner box (arms) or the outer box (shoulders and
  // melee); or `held` while it stays within the weapon's hold.
  [[nodiscard]] int find_lock(const Combatant &robot, const WeaponTuning &weapon, std::size_t slot,
                              int held = -1) const;
  [[nodiscard]] RobotControl drone_control(Combatant &drone, float seconds);
  [[nodiscard]] RobotControl ai_control(Combatant &drone, float seconds);
  void ai_sense(Combatant &drone, float seconds);
  void ai_move(Combatant &drone, float seconds);
  void ai_route(Combatant &drone, float seconds);
  void ai_pickup(Combatant &drone, float seconds);
  void ai_weapons(Combatant &drone, float seconds);
  // 0x0582b0 / 0x05ed21: the weapon records, beam sustain and forced weapons.
  void ai_setup_weapons(Combatant &drone, bool champion);
  void ai_reactions(Combatant &drone, float seconds);
  void ai_jump(Combatant &drone, float seconds); // 0x05949b's jump at a target off the sight
  [[nodiscard]] int ai_lock_target(const Combatant &drone) const; // 0x04f6a0, robot +0x130
  [[nodiscard]] FireControl ai_fire(const Combatant &drone) const;
  // Port navigation (battle_ai_steering.cpp): false leaves the original's steering.
  [[nodiscard]] const NavGraph *nav_graph();
  // Spends one of this tick's graph searches; false once they are spent.
  [[nodiscard]] bool take_nav_search();
  // Port navigation: no headway, it drops its goal or steers the original
  // way (NavTuning::escalate_seconds); true when it has just escalated.
  bool ai_escalate(Combatant &drone, float seconds);
  // Another robot stands in the way toward `to` (an offset from `drone`).
  [[nodiscard]] bool crowded(const Combatant &drone, Vec3 to) const;
  // With the grid ready, the authored climbs it cannot follow; else null.
  [[nodiscard]] const std::vector<bool> *planned_climbs();
  bool ai_plan(Combatant &drone, Vec3 goal);
  bool ai_navigate(Combatant &drone, Vec3 goal, float seconds, float speed);
  [[nodiscard]] int ai_vantage(const Combatant &drone);
  bool ai_high_ground(Combatant &drone, float seconds);
  [[nodiscard]] std::vector<bool> nav_reach(const Combatant &drone); // empty: not known
  // The highest ledge this robot's own jump climbs (battle_ai_reach.cpp).
  [[nodiscard]] float jump_reach(const Combatant &drone) const;
  [[nodiscard]] bool nav_reaches(const std::vector<bool> &reach, Vec3 point) const;
  void fire_drones(float seconds);
  void update_lock(Combatant &robot, std::size_t slot, float seconds);
  void fire_weapons(int index, const FireControl &fire, float seconds);
  static void reset_fire_inputs(Combatant &robot);
  void fired(int index, std::size_t slot, const WeaponTuning &weapon);
  void apply_hits(const std::vector<WeaponHit> &hits);
  // The rules of 0x0988e1: a hit's damage after dodges, criticals and
  // hits from behind; it may knock off a weapon. 0 when it does nothing.
  [[nodiscard]] float hit_damage(const WeaponHit &hit, Combatant &victim);
  void knock_off_weapon(Combatant &victim, Vec3 direction);
  void chatter(const Combatant &robot, int event);
  void armour_chatter(const Combatant &victim, float damage);
  void strike_chatter(const Combatant &attacker, const WeaponTuning &weapon);
  [[nodiscard]] static float ammo_share(const Combatant &robot);
  void watch_ammo(Combatant &robot);
  void watch_idle(Combatant &robot, const RobotControl &control, const FireControl &fire,
                  float seconds);
  void recover(Combatant &robot, float seconds) const;
  // `at` moved by `carry` without passing through the stage's walls.
  [[nodiscard]] Vec3 carried(const Combatant &robot, Vec3 at, Vec3 carry) const;
  float stage_floor(Combatant &robot) const;
  // `position` raised onto the ground under a robot standing there, which
  // becomes `mover`'s floor.
  [[nodiscard]] Vec3 placed(Vec3 position, float radius, RobotMover &mover) const;
  std::optional<Vec3> tracked_sight(const Combatant &robot, Vec3 crosshair) const;
  void shot_down(int victim);
  void collide(int index, float seconds);
  void pose_robot(int index, float seconds);
  void tick_death(int index, float seconds);
  void add_wreck_targets(std::vector<Target> &out) const;
  bool hit_wreck(const WeaponHit &hit);
  [[nodiscard]] Vec3 muzzle_direction(const Combatant &robot, WeaponSlot slot) const;
  [[nodiscard]] bool muzzle_blocked(const Combatant &robot, WeaponSlot slot) const;
  void separate();
  void respawn(Combatant &drone);
  void update_camouflage(Combatant &robot, bool toggle, float seconds);
  void tick_supply(float seconds);
  void supply_explosion(Vec3 at);
  void take_supply(Combatant &robot, const SupplyTaken &taken);
  // The carrier and the pickups as shot targets, after the robots.
  void add_supply_targets(std::vector<Target> &out) const;
  void add_object_targets(std::vector<Target> &out) const;
  bool hit_object(const WeaponHit &hit);
  [[nodiscard]] int object_count() const;
  // A hit on one of them; false for a robot's.
  bool hit_supply(const WeaponHit &hit);

  BattleSettings settings_;
  std::uint64_t restore_generation_{};
  int player_index_{};
  bool cooperative_{};
  bool simulation_poses_{};
  bool versus_{};
  std::map<int, std::pair<RobotControl, FireControl>> controls_;
  std::map<int, std::optional<TargetView>> target_views_;
  StageCollision stage_;
  NavGraphFuture nav_future_; // the stage's navigation grid, while it builds
  std::shared_ptr<const NavGraph> nav_;
  // Authored climbs (kind 7 segments) the grid cannot follow, by segment.
  std::vector<bool> nav_climbs_;
  int nav_plans_left_{}; // route searches left this tick (NavTuning::plans_per_tick)
  std::optional<StageDoors> doors_;
  std::optional<StageObjects> objects_;
  RobotWrecks wrecks_;
  std::optional<SupplyDrop> supply_;
  EnemyPath path_;
  std::minstd_rand random_;
  std::vector<Combatant> robots_;
  Armory armory_;
  std::vector<BattleEvent> events_;
  std::vector<int> arrivals_;
  unsigned allowed_weapons_{0xf};
  BattleCheats cheats_;
  int destroyed_{};
  bool drones_active_{true};
  bool first_kill_told_{}; // chatter_event::first_kill is said once a battle
};

} // namespace phantomcrash
