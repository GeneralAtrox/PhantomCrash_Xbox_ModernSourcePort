#include "phantomcrash/rigid_animation.hpp"
#include <algorithm>
#include <cmath>

namespace phantomcrash {
void RigidPlayback::update(const RigidRig &rig, const int requested, const float frame,
                           const bool repeating, const float delta,
                           const std::uint64_t requested_revision, const int transition_override,
                           const float speed) {
  const auto selected = requested >= 0 && requested < static_cast<int>(rig.motion_clips.size())
                            ? rig.motion_clips[static_cast<std::size_t>(requested)]
                            : std::string{};
  const float elapsed = std::max(delta, 0.0F);
  const float previous_seconds = seconds;
  const bool disabled = selected.empty() && rig.preserve_disabled_clips;
  const bool changed = !disabled && (motion != requested || revision != requested_revision);
  if (disabled) {
    seconds += elapsed;
  } else {
    if (changed) {
      if (transition_frames == 0) {
        primary_clip = clip;
      }
      transition_frames =
          requested >= 0 && requested < static_cast<int>(rig.transition_frames.size())
              ? rig.transition_frames[static_cast<std::size_t>(requested)]
              : 0;
      if (transition_override >= 0) {
        transition_frames = transition_override;
      }
      transition_frame = 0;
      transition_accumulator = 0;
      if (nodes.empty()) {
        nodes = rig.nodes;
      }
      const auto target = std::ranges::find(rig.clips, selected, &RigidClip::name);
      source = {};
      source_accumulator = 0;
      if (target != rig.clips.end()) {
        source.start(*target, 0, repeating);
      }
    }
    const bool independent =
        std::ranges::find(rig.independent_motions, requested) != rig.independent_motions.end();
    seconds = independent ? (changed ? 0.0F : seconds + elapsed) : frame / rig.frame_rate;
    clip = selected;
    loop = repeating;
    motion = requested;
    revision = requested_revision;
    source_speed = independent ? 1.0F : speed;
  }
  if (source.active()) {
    source_accumulator += elapsed * rig.frame_rate;
    const int steps = static_cast<int>(std::floor(source_accumulator + 1e-5F));
    source_accumulator = std::max(0.0F, source_accumulator - steps);
    if (nodes.empty() || (changed && transition_frames <= 0)) {
      nodes = source.sample(rig);
    }
    for (int step = 0; step < steps; ++step) {
      if (transition_frames > 0 && transition_frame >= transition_frames) {
        primary_clip = clip;
        transition_frames = transition_frame = 0;
      } else if (transition_frames > 0) {
        ++transition_frame;
      }
      const auto target = source.sample(rig);
      if (transition_frames > 0) {
        blend_rigid_pose(nodes, target, static_cast<float>(transition_frame) / transition_frames);
      } else {
        primary_clip = clip;
        nodes = target;
      }
      source.advance(source_speed, loop);
    }
    return;
  }
  if (transition_frames <= 0) {
    primary_clip = clip;
    nodes = sample_rigid_pose(rig, clip, seconds, loop);
    return;
  }
  // Model +ec advances per update, independently of the motion's speed.
  // Re-evaluate at the source rate; attachment/draw queries never advance it.
  transition_accumulator += elapsed * rig.frame_rate;
  const int steps = static_cast<int>(std::floor(transition_accumulator + 1e-5F));
  transition_accumulator = std::max(0.0F, transition_accumulator - steps);
  const float start = changed ? std::max(0.0F, seconds - elapsed) : previous_seconds;
  for (int step = 0; step < steps; ++step) {
    if (transition_frame >= transition_frames) {
      primary_clip = clip;
      transition_frames = 0;
      transition_frame = 0;
      transition_accumulator = 0;
      nodes = sample_rigid_pose(rig, clip, seconds, loop);
      return;
    }
    ++transition_frame;
    const float time = start + (seconds - start) * static_cast<float>(step + 1) / steps;
    const auto target = sample_rigid_pose(rig, clip, time, loop);
    // Source +b0/+c0/+d0 store each evaluated pose and feed it back on the
    // next update, including when another transition interrupts this one.
    blend_rigid_pose(nodes, target, static_cast<float>(transition_frame) / transition_frames);
  }
}

void RigidPlayback::update_weapon(const RigidRig &rig, const float delta, const bool started,
                                  const bool restart) {
  const float elapsed = std::max(delta, 0.0F);
  if (!started) {
    nodes = rig.nodes;
    return;
  }
  if (restart || clip.empty()) {
    clip = rig.weapon_clip;
    seconds = 0;
    source_accumulator = 0;
    source = {};
    const auto target = std::ranges::find(rig.clips, clip, &RigidClip::name);
    if (target != rig.clips.end()) {
      source.start(*target);
    }
    nodes = source.active() ? source.sample(rig) : sample_rigid_pose(rig, clip, 0, false);
  } else {
    seconds += elapsed;
  }
  loop = false;
  if (!source.active()) {
    nodes = sample_rigid_pose(rig, clip, seconds, false);
    return;
  }
  source_accumulator += elapsed * rig.frame_rate;
  const int steps = static_cast<int>(std::floor(source_accumulator + 1e-5F));
  source_accumulator = std::max(0.0F, source_accumulator - steps);
  for (int step = 0; step < steps; ++step) {
    nodes = source.sample(rig);
    source.advance(1, false);
  }
}
} // namespace phantomcrash
