#include "state_sections.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/robot_combat.hpp"
#include "phantomcrash/rumbling_world.hpp"

// Each section names one member the matching StateInspector::write covers.
#define PC_PART(member) out.add(#member, v.member)

namespace phantomcrash {
void StateInspector::sections(online::StateSections &out, const Battle &v) {
  out.begin("battle");
  PC_PART(settings_);
  out.add("player_index_", v.cooperative_ ? -1 : v.player_index_);
  PC_PART(cooperative_);
  PC_PART(simulation_poses_);
  PC_PART(versus_);
  PC_PART(controls_);
  PC_PART(target_views_);
  PC_PART(stage_);
  out.add("nav_", v.nav_ != nullptr);
  PC_PART(nav_climbs_);
  PC_PART(nav_plans_left_);
  PC_PART(doors_);
  PC_PART(objects_);
  PC_PART(wrecks_);
  PC_PART(supply_);
  PC_PART(path_);
  PC_PART(random_);
  PC_PART(robots_);
  PC_PART(armory_);
  PC_PART(events_);
  PC_PART(arrivals_);
  PC_PART(allowed_weapons_);
  PC_PART(cheats_);
  PC_PART(destroyed_);
  PC_PART(drones_active_);
  PC_PART(first_kill_told_);
  PC_PART(players_spared_);
  out.show("robots", v.robots_.size());
  out.show("events", v.events_.size());
  sections(out, v.armory_);
  for (std::size_t index = 0; index < v.robots_.size(); ++index) {
    sections(out, v.robots_[index], "robot[" + std::to_string(index) + "]");
  }
}

void StateInspector::sections(online::StateSections &out, const Armory &v) {
  out.begin("armory");
  PC_PART(projectiles_);
  PC_PART(next_projectile_id_);
  PC_PART(blasts_);
  PC_PART(beams_);
  PC_PART(explosions_);
  PC_PART(landings_);
  PC_PART(random_);
  out.show("projectiles", v.projectiles_.size());
  out.show("next_projectile_id", v.next_projectile_id_);
}

void StateInspector::sections(online::StateSections &out, const Combatant &v,
                              const std::string &label) {
  out.begin(label);
  out.add("all", v);
  out.show("position", v.mover.position());
  out.show("heading", v.mover.heading());
  out.show("hp", v.hp);
  out.show("alive", v.alive);
  out.show("dying", v.dying);
  out.show("kills", v.kills);
  out.show("shot_by", v.shot_by);
  out.show("piloted", v.piloted);
  out.show("drone", v.drone);
  out.show("team", v.team);
  out.begin(label + ".fields");
  PC_PART(pose);
  PC_PART(pose_updated);
  PC_PART(name);
  PC_PART(tag);
  PC_PART(leader);
  PC_PART(player_number);
  PC_PART(team);
  PC_PART(marker_colour);
  PC_PART(firing);
  PC_PART(trigger_held);
  PC_PART(rival);
  PC_PART(boss);
  PC_PART(mover);
  PC_PART(radius);
  PC_PART(stage_contact);
  PC_PART(floor_surface);
  PC_PART(last_ground);
  PC_PART(fence_contact_left);
  PC_PART(piloted);
  PC_PART(hp);
  PC_PART(max_hp);
  PC_PART(alive);
  PC_PART(dying);
  PC_PART(drone);
  PC_PART(shot_by);
  PC_PART(kills);
  PC_PART(death_seconds);
  PC_PART(frozen);
  PC_PART(respawn_left);
  PC_PART(auto_respawn);
  PC_PART(weapons);
  PC_PART(cooldown);
  PC_PART(magazines);
  PC_PART(lock_candidates);
  PC_PART(lock_held);
  PC_PART(locks);
  PC_PART(lock_seconds);
  PC_PART(sight);
  PC_PART(strike_slot);
  PC_PART(source_free_arm_action);
  PC_PART(queued_rounds);
  PC_PART(queue_left);
  PC_PART(lost);
  PC_PART(combat);
  PC_PART(invulnerable);
  PC_PART(shield);
  PC_PART(shield_glow);
  PC_PART(push);
  PC_PART(damage_from);
  PC_PART(hit_flash);
  PC_PART(shown_armour);
  PC_PART(camouflaged);
  PC_PART(camouflage);
  PC_PART(camouflage_sweep);
  PC_PART(camouflage_broken);
  PC_PART(ammo_share);
  PC_PART(idle_seconds);
  PC_PART(path);
  PC_PART(ai);
  sections(out, v.mover, label + ".mover");
  if (v.pose) {
    sections(out, *v.pose, label + ".pose");
  }
}

void StateInspector::sections(online::StateSections &out, const RobotMover &v,
                              const std::string &label) {
  out.begin(label);
  PC_PART(tuning_);
  PC_PART(position_);
  PC_PART(heading_);
  PC_PART(twist_);
  PC_PART(mouse_yaw_);
  PC_PART(twist_stick_);
  PC_PART(twist_push_);
  PC_PART(twist_reset_from_);
  PC_PART(twist_reset_left_);
  PC_PART(quick_turn_left_);
  PC_PART(quick_turn_cooldown_);
  PC_PART(pitch_);
  PC_PART(mode_);
  PC_PART(motion_);
  PC_PART(motion_revision_);
  PC_PART(loops_);
  PC_PART(motion_speed_);
  PC_PART(mode_seconds_);
  PC_PART(backward_);
  PC_PART(grounded_);
  PC_PART(floor_);
  PC_PART(local_velocity_);
  PC_PART(coast_velocity_);
  PC_PART(coast_brake_);
  PC_PART(vertical_speed_);
  PC_PART(dash_hold_);
  PC_PART(lift_left_);
  PC_PART(lift_spent_);
  PC_PART(down_boosting_);
  PC_PART(jump_was_held_);
  PC_PART(stick_);
  PC_PART(rolling_steer_);
  PC_PART(stagger_left_);
  PC_PART(strike_armed_frames_);
  PC_PART(strike_last_frames_);
  PC_PART(lunge_at_);
  PC_PART(lunge_height_);
  PC_PART(lunge_piloted_);
  PC_PART(dodged_);
  PC_PART(flick_state_);
  PC_PART(flick_left_);
  PC_PART(timings_);
  PC_PART(leg_type_);
  PC_PART(dash_steer_);
  PC_PART(dodge_velocity_);
  PC_PART(dodge_slide_frames_);
  PC_PART(dodge_ease_frames_);
  PC_PART(dodge_last_frames_);
}

void StateInspector::sections(online::StateSections &out, const RobotCombat &v,
                              const std::string &label) {
  out.begin(label);
  PC_PART(movement_mounts_);
  PC_PART(movement_groups_);
  PC_PART(assembly_);
  PC_PART(animator_);
  PC_PART(motion_revision_);
  PC_PART(tuning_);
  PC_PART(sockets_);
  PC_PART(muzzles_);
  PC_PART(barrel_);
  PC_PART(weights_);
  PC_PART(auxiliary_);
  PC_PART(wheel_turn_);
  PC_PART(shoulders_);
  PC_PART(pose_);
}

void StateInspector::sections(online::StateSections &out, const RumblingWorld &v) {
  out.begin("world");
  out.add("check_.rules", v.check_.rules);
  out.add("check_.open_contests", v.check_.open_contests);
  out.add("tables_.rules", v.tables_.rules);
  out.add("tables_.hud", v.tables_.hud);
  PC_PART(settings_);
  PC_PART(stages_);
  PC_PART(live_);
  PC_PART(random_);
  PC_PART(epoch_);
  PC_PART(areas_);
  PC_PART(lent_);
  PC_PART(background_);
  PC_PART(deterministic_);
  PC_PART(lent_looks_);
  out.show("epoch", v.epoch_);
  out.show("lent", v.lent_);
  for (std::size_t index = 0; index < v.areas_.size(); ++index) {
    const auto &area = v.areas_[index];
    out.begin("world.area[" + std::to_string(index) + "]");
    out.add("battle", area.battle);
    out.add("roster", area.roster);
    out.add("robots", area.robots);
    out.add("clock", area.clock);
    out.add("changes", area.changes);
    out.add("enemy_level_offset", area.enemy_level_offset);
    out.add("player_kills", area.player_kills);
    out.add("player_bounty", area.player_bounty);
    out.show("clock", area.clock);
    out.show("changes", area.changes);
    out.show("robots", area.robots.size());
  }
}
} // namespace phantomcrash
