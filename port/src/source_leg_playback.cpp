#include "phantomcrash/source_leg_playback.hpp"
#include "phantomcrash/skeleton.hpp"
#include "phantomcrash/weapon_mounts.hpp"
#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {
constexpr float native_control_updates_per_second = 60.0F;
Mat4 control_pose(const SkeletonNode &node, const TrackSample &sample,
                  const std::optional<Vec3> translation_override = {}) {
  Vec3 rotation{-node.rotation.x, -node.rotation.y, node.rotation.z};
  Vec3 position{node.translation.x, node.translation.y, -node.translation.z};
  if (sample.rotation) {
    rotation = {-sample.rotation->x, sample.rotation->y, -sample.rotation->z};
  }
  if (sample.translation && !node.source_model_root) {
    position = {sample.translation->x, -sample.translation->y, sample.translation->z};
  }
  if (translation_override) {
    position = *translation_override;
  }
  return rotation_x(rotation.x) * rotation_y(rotation.y) * rotation_z(rotation.z) *
         translation(position);
}

const SourcePmtTrack *pmt_track(const Motion *motion, const int track) {
  if (!motion || track < 0 || static_cast<std::size_t>(track) >= motion->tracks.size()) {
    return nullptr;
  }
  const auto &pmt = motion->tracks[static_cast<std::size_t>(track)].source_pmt;
  return pmt ? &*pmt : nullptr;
}

Mat4 clip_pose(const SkeletonNode &node, const Motion *motion, const float frame) {
  const auto *pmt = pmt_track(motion, node.track);
  if (!pmt) {
    return control_pose(node, motion ? motion->sample(node.track, frame) : TrackSample{});
  }
  Mat4 pose = sample_source_pmt(*pmt, frame);
  if (node.source_model_root) {
    pose.at(3, 0) = node.translation.x;
    pose.at(3, 1) = node.translation.y;
    pose.at(3, 2) = -node.translation.z;
  }
  return pose;
}

Mat4 native_adapter(const SkeletonNode &node, const MotionSource &source) {
  if (!source.blending()) {
    return clip_pose(node, source.motion, source.frame);
  }
  const Mat4 current = control_pose(node, source.sample(node.track));
  const Vec3 position{current.at(3, 0), current.at(3, 1), current.at(3, 2)};
  const auto at_position = [&](Mat4 pose) {
    pose.at(3, 0) = position.x;
    pose.at(3, 1) = position.y;
    pose.at(3, 2) = position.z;
    return pose;
  };
  const auto sample = [&](const MotionLayer &layer) {
    return at_position(clip_pose(node, layer.motion, layer.frame));
  };
  const auto blend = [](const Mat4 &a, const Mat4 &b, const float weight) {
    return blend_pose_transform(a, b, weight);
  };
  const auto previous = source.blend_from<Mat4>(sample, blend);
  const Mat4 target = at_position(clip_pose(node, source.motion, source.frame));
  return previous ? blend(*previous, target, source.weight) : target;
}
} // namespace

void SourceLegPlayback::reset(const SkeletonNode &root) {
  controls_.clear();
  locals_.clear();
  frames_ = frame_ = 0;
  accumulator_ = 0;
  sample_frame_ = 0;
  start_frame_ = 0;
  motion_ = nullptr;
  cursors_.clear();
  const auto collect = [&](const auto &self, const SkeletonNode &node, const bool source) -> void {
    const bool inside = source || node.source_model_root;
    if (inside && node.track >= 0) {
      const auto track = static_cast<std::size_t>(node.track);
      controls_.resize(std::max(controls_.size(), track + 1));
      locals_.resize(controls_.size());
      controls_[track] = &node;
      locals_[track] = control_pose(node, {});
    }
    for (const auto &child : node.children) {
      self(self, child, inside);
    }
  };
  collect(collect, root, false);
}

void SourceLegPlayback::play(const int transition_frames, const float start_frame,
                             const Motion *motion, const bool loop) {
  frames_ = std::max(transition_frames, 0);
  frame_ = 0;
  accumulator_ = 0;
  sample_frame_ = start_frame;
  start_frame_ = start_frame;
  motion_ = motion;
  cursors_.assign(controls_.size(), {});
  for (std::size_t track = 0; track < controls_.size(); ++track) {
    if (const auto *pmt = pmt_track(motion, static_cast<int>(track))) {
      cursors_[track].reset(*pmt, start_frame, loop);
    }
  }
}

void SourceLegPlayback::advance(const MotionSource &source, const float seconds,
                                const bool original_transition, const bool loop,
                                const float speed) {
  if (motion_ != source.motion || cursors_.size() != controls_.size()) {
    motion_ = source.motion;
    cursors_.assign(controls_.size(), {});
    for (std::size_t track = 0; track < controls_.size(); ++track) {
      if (const auto *pmt = pmt_track(motion_, static_cast<int>(track))) {
        cursors_[track].reset(*pmt, start_frame_, loop);
      }
    }
  }
  if (!original_transition) {
    for (std::size_t track = 0; track < controls_.size(); ++track) {
      if (controls_[track]) {
        locals_[track] = native_adapter(*controls_[track], source);
      }
    }
    sample_frame_ = source.frame;
    return;
  }
  accumulator_ += std::max(seconds, 0.0F) * native_control_updates_per_second;
  const int steps = static_cast<int>(std::floor(accumulator_ + 1e-5F));
  accumulator_ = std::max(0.0F, accumulator_ - steps);
  float travel = source.frame - sample_frame_;
  if (travel < 0 && source.motion && source.motion->frames > 0) {
    travel += source.motion->frames;
  }
  for (int step = 0; step < steps; ++step) {
    if (frame_ >= frames_) {
      frames_ = frame_ = 0;
    } else {
      ++frame_;
    }
    const float weight = frames_ > 0 ? static_cast<float>(frame_) / frames_ : 1.0F;
    float sample_frame = sample_frame_ + travel * static_cast<float>(step + 1) / steps;
    if (source.motion && source.motion->frames > 0 && sample_frame > source.motion->frames) {
      sample_frame = std::fmod(sample_frame, static_cast<float>(source.motion->frames));
    }
    for (std::size_t track = 0; track < controls_.size(); ++track) {
      const auto *node = controls_[track];
      if (!node) {
        continue;
      }
      const auto *pmt = pmt_track(source.motion, static_cast<int>(track));
      Mat4 target =
          pmt ? cursors_[track].sample(*pmt) : clip_pose(*node, source.motion, sample_frame);
      if (node->source_model_root) {
        target.at(3, 0) = node->translation.x;
        target.at(3, 1) = node->translation.y;
        target.at(3, 2) = -node->translation.z;
      }
      locals_[track] = weight >= 1 ? target : blend_pose_transform(*locals_[track], target, weight);
      if (pmt) {
        // 0x19eeb4 evaluates before 0x19eee4/0x19ef14 advance the two
        // key cursors. The incoming secondary uses mode 0x40 here.
        cursors_[track].advance(*pmt, speed, loop);
      }
    }
  }
  sample_frame_ = source.frame;
}

std::optional<Mat4> source_leg_transform(const SkeletonNode &node, const PoseInput &input,
                                         const int part) {
  if (part != 0 || !input.source_leg_locals || node.track < 0 ||
      static_cast<std::size_t>(node.track) >= input.source_leg_locals->size()) {
    return {};
  }
  const auto &cached = (*input.source_leg_locals)[static_cast<std::size_t>(node.track)];
  if (!cached) {
    return {};
  }
  if ((node.flags & node_wheel) != 0 && !(node.source_roll && node.source_roll->spherical)) {
    const float angle = node.source_roll && input.source_roll_angles
                            ? (*input.source_roll_angles)[static_cast<std::size_t>(node.track)]
                            : input.wheel_turn;
    return rotation_x(angle) * *cached;
  }
  return cached;
}
} // namespace phantomcrash
