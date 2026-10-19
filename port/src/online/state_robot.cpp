#include "state_writer.hpp"
#include "phantomcrash/robot_combat.hpp"

namespace phantomcrash {
void StateInspector::write(online::StateWriter &out, const RobotMover &v) {
  out(v.tuning_, v.position_, v.heading_, v.twist_, v.mouse_yaw_, v.twist_stick_, v.twist_push_,
      v.twist_reset_from_, v.twist_reset_left_, v.quick_turn_left_, v.quick_turn_cooldown_,
      v.pitch_, v.mode_, v.motion_, v.motion_revision_, v.loops_, v.motion_speed_, v.mode_seconds_,
      v.backward_, v.grounded_, v.floor_, v.local_velocity_, v.coast_velocity_, v.coast_brake_,
      v.vertical_speed_, v.dash_hold_, v.lift_left_, v.lift_spent_, v.down_boosting_,
      v.jump_was_held_, v.stick_, v.rolling_steer_, v.stagger_left_, v.strike_armed_frames_,
      v.strike_last_frames_, v.lunge_at_, v.lunge_height_, v.lunge_piloted_, v.dodged_,
      v.flick_state_, v.flick_left_, v.timings_, v.leg_type_, v.dash_steer_, v.dodge_velocity_,
      v.dodge_slide_frames_, v.dodge_ease_frames_, v.dodge_last_frames_);
}

void StateInspector::write(online::StateWriter &out, const RobotCombat &v) {
  out(v.movement_mounts_, v.movement_groups_, v.assembly_, v.animator_, v.motion_revision_,
      v.tuning_, v.sockets_, v.muzzles_, v.barrel_, v.weights_, v.auxiliary_, v.wheel_turn_,
      v.shoulders_, v.pose_);
}

void StateInspector::write(online::StateWriter &out, const RobotAnimator &v) {
  out(v.assembly_, v.legs_, v.body_, v.auxiliary_frames_, v.arms_, v.motion_, v.loop_, v.frame_,
      v.keep_sway_, v.wheel_turn_, v.procedural_seconds_, v.source_roll_, v.source_legs_,
      v.source_transition_, v.source_actor_idle_, v.has_source_roll_, v.previous_,
      v.previous_count_, v.speed_, v.fade_, v.fade_frames_);
}

void StateInspector::write(online::StateWriter &out, const SourceLegPlayback &v) {
  out(v.controls_, v.locals_, v.frames_, v.frame_, v.accumulator_, v.sample_frame_, v.start_frame_,
      v.motion_, v.cursors_);
}
void StateInspector::write(online::StateWriter &out, const SourceRollState &v) {
  out(v.accumulators_, v.angles_, v.spherical_, v.frames_, v.previous_position_);
}
} // namespace phantomcrash
