#include "state_writer.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/robot_combat.hpp"

namespace phantomcrash {
void StateInspector::write(online::StateWriter &out, const Battle &v) {
  if (v.nav_future_.valid() && !v.nav_) {
    throw std::runtime_error("Prepare online navigation before inspecting battle state");
  }
  // restore_generation_ only invalidates draw caches. In online co-op the
  // selected player is a local view cursor, overwritten before each shared step.
  out(v.settings_, v.cooperative_ ? -1 : v.player_index_, v.cooperative_, v.simulation_poses_,
      v.versus_, v.controls_, v.target_views_, v.stage_);
  // Navigation is built once per stage; its contents digest is remembered.
  out(v.nav_ != nullptr);
  if (v.nav_) {
    out(online::remembered_digest(
        v.nav_, [](online::StateWriter &nav, const NavGraph &graph) { nav(graph); }));
  }
  out(v.nav_climbs_, v.nav_plans_left_, v.doors_, v.objects_, v.wrecks_, v.supply_, v.path_,
      v.random_, v.robots_, v.armory_, v.events_, v.arrivals_, v.allowed_weapons_, v.cheats_,
      v.destroyed_, v.drones_active_, v.first_kill_told_, v.players_spared_);
}

void StateInspector::write(online::StateWriter &out, const Combatant &v) {
  out(v.pose, v.pose_updated, v.name, v.tag, v.leader, v.player_number, v.team, v.marker_colour,
      v.firing, v.trigger_held, v.piloted, v.rival, v.boss, v.mover, v.radius, v.stage_contact,
      v.floor_surface, v.last_ground, v.fence_contact_left, v.hp, v.max_hp, v.alive, v.dying,
      v.death_seconds, v.drone, v.frozen, v.respawn_left, v.auto_respawn, v.shot_by, v.weapons,
      v.cooldown, v.magazines, v.lock_candidates, v.lock_held, v.locks, v.lock_seconds, v.sight,
      v.strike_slot, v.source_free_arm_action, v.queued_rounds, v.queue_left, v.lost, v.combat,
      v.invulnerable, v.shield, v.shield_glow, v.push, v.damage_from, v.kills, v.hit_flash,
      v.shown_armour, v.camouflaged, v.camouflage, v.camouflage_sweep, v.camouflage_broken,
      v.ammo_share, v.idle_seconds, v.path, v.ai);
}

void StateInspector::write(online::StateWriter &out, const Armory &v) {
  out(v.projectiles_, v.next_projectile_id_, v.blasts_, v.beams_, v.explosions_, v.landings_,
      v.random_);
}

void StateInspector::write(online::StateWriter &out, const PathFollower &v) {
  out(v.segment_, v.point_);
}

void StateInspector::write(online::StateWriter &out, const ShownArmour &v) {
  out(v.started_, v.shown_, v.from_, v.target_, v.left_, v.length_);
}

namespace online {
StateDigest state_digest(const Battle &battle) {
  StateWriter out;
  out(std::uint32_t{1}, battle);
  return out.finish();
}
} // namespace online
} // namespace phantomcrash
