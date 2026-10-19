#pragma once

#include "phantomcrash/math3d.hpp"
#include "phantomcrash/source_pmt.hpp"
#include <optional>
#include <vector>

namespace phantomcrash {
struct SkeletonNode;
struct MotionSource;
struct Motion;
struct PoseInput;

// Original leg controls evaluate channel poses before rolling/suspension
// callbacks. Each simulation update feeds its result into the next blend.
class SourceLegPlayback {
  friend class StateInspector;
public:
  void reset(const SkeletonNode &root);
  void play(int transition_frames, float start_frame = 0, const Motion *motion = nullptr,
            bool loop = true);
  void advance(const MotionSource &source, float seconds, bool original_transition,
               bool loop = true, float speed = 1);
  [[nodiscard]] const std::vector<std::optional<Mat4>> &locals() const {
    return locals_;
  }
  [[nodiscard]] int transition_frame() const {
    return frame_;
  }
  [[nodiscard]] int transition_frames() const {
    return frames_;
  }

private:
  std::vector<const SkeletonNode *> controls_;
  std::vector<std::optional<Mat4>> locals_;
  int frames_{};
  int frame_{};
  float accumulator_{};
  float sample_frame_{};
  float start_frame_{};
  const Motion *motion_{};
  std::vector<SourcePmtCursor> cursors_;
};

// Called by the skeleton evaluator; applies source wheel callbacks after the
// cached channel pose and never advances a clock or changes the cached pose.
[[nodiscard]] std::optional<Mat4> source_leg_transform(const SkeletonNode &node,
                                                       const PoseInput &input, int part);
} // namespace phantomcrash
