#include "phantomcrash/robot_combat.hpp"

namespace phantomcrash {

RobotAnimator::RobotAnimator(const RobotAnimator &source, const RobotAssembly &assembly)
    : assembly_(assembly), legs_(source.legs_), body_(source.body_),
      auxiliary_frames_(source.auxiliary_frames_), arms_(source.arms_), motion_(source.motion_),
      loop_(source.loop_), frame_(source.frame_), keep_sway_(source.keep_sway_),
      wheel_turn_(source.wheel_turn_), procedural_seconds_(source.procedural_seconds_),
      source_roll_(source.source_roll_), source_legs_(source.source_legs_),
      source_transition_(source.source_transition_), source_actor_idle_(source.source_actor_idle_),
      has_source_roll_(source.has_source_roll_), previous_(source.previous_),
      previous_count_(source.previous_count_), speed_(source.speed_), fade_(source.fade_),
      fade_frames_(source.fade_frames_) {}

RobotCombat::RobotCombat(const RobotCombat &source)
    : movement_mounts_(source.movement_mounts_), movement_groups_(source.movement_groups_),
      assembly_(source.assembly_), animator_(source.animator_, assembly_),
      motion_revision_(source.motion_revision_), tuning_(source.tuning_), sockets_(source.sockets_),
      muzzles_(source.muzzles_), barrel_(source.barrel_), weights_(source.weights_),
      auxiliary_(source.auxiliary_), wheel_turn_(source.wheel_turn_), shoulders_(source.shoulders_),
      pose_(source.pose_) {}

} // namespace phantomcrash
