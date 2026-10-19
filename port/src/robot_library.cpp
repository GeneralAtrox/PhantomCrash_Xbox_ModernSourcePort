#include "phantomcrash/robot_library.hpp"

#include <algorithm>
#include <cmath>
#include <exception>

namespace phantomcrash {

RobotLibrary::RobotLibrary(const DataRoots &roots) : roots_(roots) {
  try {
    catalogue_ = parse_robot_catalogue(json::read_file(roots.asset("robots/catalogue.json")));
    skeletons_ = parse_robot_skeletons(json::read_file(roots.asset("robots/skeletons.json")));
    read_maker_tables(roots.document("ui/robot_preview.json"), catalogue_);
    available_ = true;
  } catch (const std::exception &error) {
    errors_.push_back(std::string("Robots are unavailable: ") + error.what());
  }
  try {
    if (const auto file = roots.find_asset("robots/weapon_mounts.json")) {
      mounts_ = parse_weapon_mounts(json::read_file(*file));
    } else {
      errors_.push_back("Weapon mounts are missing; regenerate the robot extraction");
    }
    if (const auto file = roots.find_asset("robots/movement_mounts.json")) {
      movement_ = parse_movement_mounts(json::read_file(*file));
    } else {
      errors_.push_back("Movement mounts are missing; regenerate the robot extraction");
    }
    combat_ = parse_combat_animation(roots.document("world/robot_animation.json"));
  } catch (const std::exception &error) {
    errors_.push_back(std::string("Robot combat data: ") + error.what());
  }
}

const MotionBank *RobotLibrary::bank(const std::string &asset_path) {
  if (const auto found = banks_.find(asset_path); found != banks_.end()) {
    return found->second.get();
  }
  std::unique_ptr<MotionBank> bank;
  if (!asset_path.empty()) {
    try {
      bank = std::make_unique<MotionBank>(
          parse_motion_bank(json::read_file(roots_.asset(asset_path))));
    } catch (const std::exception &error) {
      errors_.push_back("Motion bank " + asset_path + ": " + error.what());
    }
  }
  return (banks_[asset_path] = std::move(bank)).get();
}

RobotAnimator::RobotAnimator(RobotLibrary &library, const RobotAssembly &assembly)
    : assembly_(assembly), legs_(library.bank(assembly.leg_motions)),
      body_(library.bank(assembly.body_motions)) {
  has_source_roll_ = assembly.skeleton && has_source_roll(*assembly.skeleton);
  if (assembly.skeleton && !assembly.source_transition_frames.empty()) {
    source_legs_.reset(*assembly.skeleton);
  }
  for (std::size_t side = 0; side < 2; ++side) {
    arms_[side] = library.bank(assembly.arm_motions[side]);
  }
}

MotionTiming RobotAnimator::timing(const int motion) const {
  if (motion >= 0 && motion < static_cast<int>(assembly_.timings.size())) {
    return assembly_.timings[static_cast<std::size_t>(motion)];
  }
  const Motion *clip = legs_ != nullptr ? legs_->motion(motion) : nullptr;
  const float last = clip != nullptr ? static_cast<float>(clip->frames) : 0.0F;
  return {0.0F, 0.0F, last, last};
}

void RobotAnimator::play(const int motion, const bool loop, const float fade_frames,
                         const float start_frame, const bool restart) {
  const bool mapped = motion >= 0 &&
                      motion < static_cast<int>(assembly_.source_motion_actions.size()) &&
                      assembly_.source_motion_actions[static_cast<std::size_t>(motion)] >= 0;
  if ((mapped || std::ranges::find(assembly_.source_melee_motions, motion) !=
                     assembly_.source_melee_motions.end()) &&
      legs_ && !legs_->motion(motion)) {
    return; // a disabled source action leaves this model's current playback intact
  }
  if (motion == motion_ && !restart) {
    loop_ = loop;
    return;
  }
  source_transition_ = mapped && !assembly_.source_transition_frames.empty();
  int duration =
      source_transition_ && motion < static_cast<int>(assembly_.source_transition_frames.size())
          ? assembly_.source_transition_frames[static_cast<std::size_t>(motion)]
          : 0;
  if ((motion == 8 || motion == 9) && assembly_.source_turn_frames[0] >= 0) {
    duration = assembly_.source_turn_frames[source_actor_idle_ ? 0 : 1];
  }
  // Zero-duration secondary/queued actions still use their existing adapter
  // until the source completion/promotion producer is connected.
  source_transition_ = source_transition_ && duration > 0;
  source_legs_.play(duration, start_frame, legs_ ? legs_->motion(motion) : nullptr, loop);
  if (motion < 37 || motion > 44) {
    source_actor_idle_ = motion == 28;
  }
  // What shows now is frozen to blend from: the motion alone, or, when a
  // change interrupts a blend, that blend whole, so the pose does not jump.
  if (fade_ >= 1.0F) {
    previous_count_ = 0;
  }
  if (motion_ >= 0) {
    if (previous_count_ == previous_.size()) {
      std::shift_left(previous_.begin(), previous_.end(), 1); // the oldest gives way
      --previous_count_;
    }
    previous_[previous_count_] = {motion_, frame_, previous_count_ == 0 ? 1.0F : fade_, keep_sway_};
    ++previous_count_;
  }
  motion_ = motion;
  loop_ = loop;
  frame_ = start_frame;
  fade_frames_ = std::max(fade_frames, 0.0F);
  fade_ = previous_count_ > 0 && fade_frames_ > 0.0F ? 0.0F : 1.0F;
  if (fade_ >= 1.0F) {
    previous_count_ = 0;
  }
}

void RobotAnimator::tick(const double seconds) {
  procedural_seconds_ += std::max(seconds, 0.0);
  const auto frames = static_cast<float>(seconds) * motion_frames_per_second * speed_;
  const MotionTiming t = timing(motion_);
  frame_ += frames;
  if (loop_ && t.loop_end > t.loop_start) {
    while (frame_ > t.loop_end) {
      frame_ -= t.loop_end - t.loop_start;
    }
  } else {
    frame_ = std::min(frame_, t.last);
  }
  if (fade_ < 1.0F) {
    fade_ = std::min(1.0F, fade_ + frames / std::max(fade_frames_, 1e-3F));
  }
  source_legs_.advance(source(legs_), static_cast<float>(seconds), source_transition_, loop_,
                       speed_);
}

bool RobotAnimator::finished() const noexcept {
  return !loop_ && frame_ >= timing(motion_).last;
}

MotionSource RobotAnimator::source(const MotionBank *bank) const {
  MotionSource out;
  if (bank == nullptr) {
    return out;
  }
  out.motion = bank->motion(motion_);
  out.frame = frame_;
  if (fade_ < 1.0F) {
    for (std::size_t i = 0; i < previous_count_; ++i) {
      const Layer &layer = previous_[i];
      out.previous[i] = {bank->motion(layer.motion), layer.frame, layer.weight, layer.keep_sway};
    }
    out.previous_count = previous_count_;
    out.weight = fade_;
  }
  return out;
}

void RobotAnimator::pose(const Mat4 &root, const float aim_yaw, const float aim_pitch,
                         const bool keep_sway, Pose &out, const std::array<ArmAim, 2> &arms,
                         const std::array<int, 2> &firing_poses) const {
  if (assembly_.skeleton == nullptr) {
    for (PosePart &part : out) {
      part.set.fill(false);
    }
    return;
  }
  PoseInput input;
  input.root = root;
  input.legs = source(legs_);
  for (std::size_t slot = 0; body_ && slot < input.body_auxiliary.size(); ++slot) {
    input.body_auxiliary[slot].motion = body_->motion(static_cast<int>(slot));
    input.body_auxiliary[slot].frame = auxiliary_frames_[slot < 2 ? slot : 2];
  }
  for (std::size_t side = 0; side < 2; ++side) {
    input.arms[side] = source(arms_[side]);
    input.firing_poses[side].motion =
        arms_[side] ? arms_[side]->motion(firing_poses[side]) : nullptr;
  }
  input.arm_skeletons = assembly_.arm_skeletons;
  input.arm_aim = arms;
  input.aim_yaw = aim_yaw;
  input.aim_pitch = aim_pitch;
  input.forearm_turn = assembly_.forearm_turn;
  input.keep_sway = keep_sway;
  input.wheel_turn = wheel_turn_;
  input.procedural_seconds = procedural_seconds_;
  input.source_roll_angles = &source_roll_.angles();
  input.source_roll_frames = &source_roll_.frames();
  input.source_leg_locals = &source_legs_.locals();
  build_pose(*assembly_.skeleton, input, out);
}

void RobotAnimator::advance_source_roll(const RobotMover &mover, const float seconds) {
  if (!has_source_roll_) {
    return;
  }
  Pose unrolled;
  pose(rotation_y(mover.heading()) * translation(mover.position()), 0, 0, keep_sway_, unrolled);
  source_roll_.advance(*assembly_.skeleton, unrolled[0].bones, unrolled[0].set, mover, seconds);
}

float stride_speed(const MotionBank &bank, const RobotAssembly &assembly, const int motion) {
  const Motion *clip = bank.motion(motion);
  if (clip == nullptr || assembly.skeleton == nullptr) {
    return 0.0F;
  }
  const int track = root_motion_track(*assembly.skeleton);
  if (track < 0) {
    return 0.0F;
  }
  float start = 0.0F;
  float end = static_cast<float>(clip->frames);
  if (motion >= 0 && motion < static_cast<int>(assembly.timings.size())) {
    const MotionTiming &timing = assembly.timings[static_cast<std::size_t>(motion)];
    if (timing.loop_end > timing.loop_start) {
      start = timing.loop_start;
      end = timing.loop_end;
    }
  }
  const TrackSample from = clip->sample(track, start);
  const TrackSample to = clip->sample(track, end);
  if (!from.translation || !to.translation || end <= start) {
    return 0.0F;
  }
  const float dx = to.translation->x - from.translation->x;
  const float dz = to.translation->z - from.translation->z;
  return std::sqrt(dx * dx + dz * dz) * motion_frames_per_second / (end - start);
}

MovementTuning stride_tuning(RobotLibrary &library, const RobotAssembly &assembly,
                             MovementTuning base) {
  const MotionBank *bank = library.bank(assembly.leg_motions);
  if (bank == nullptr) {
    return base;
  }
  const auto take = [&](const int motion, float &speed) {
    const float stride = stride_speed(*bank, assembly, motion);
    if (stride > 0.0F) {
      speed = stride;
    }
    return stride > 0.0F;
  };
  base.walk_stride = take(robot_motion::walk, base.walk_speed);
  take(robot_motion::walk_back, base.back_speed);
  base.dash_stride = take(robot_motion::dash, base.dash_speed);
  take(robot_motion::dash_back, base.dash_back_speed);
  return base;
}

} // namespace phantomcrash
