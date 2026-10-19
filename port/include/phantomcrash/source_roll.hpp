#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"
#include <array>
#include <optional>

namespace phantomcrash {
struct SkeletonNode;
class RobotMover;

struct SourceRollControl {
  int group{};
  float coefficient{};
  float pi{};
  float max_step{};
  float source_units_per_native{};
  bool spherical{};
  Vec3 baked_mesh_scale{1, 1, 1};
  float steering_radians_per_degree{0.01745329238474369F}; // source 0x3c8efa35
  float native_clock_rate{60}; // Native seconds to frame-clock input adapter.
};

[[nodiscard]] std::optional<SourceRollControl> parse_source_roll(const json::JsonValue &node);
[[nodiscard]] bool has_source_roll(const SkeletonNode &node);
void apply_source_spherical_roll(Mat4 &world, const SkeletonNode &node, const Mat4 &model_root,
                                 const std::array<std::optional<Mat4>, 64> *frames);

class SourceRollState {
  friend class StateInspector;
public:
  void advance(const SkeletonNode &rig, const std::array<Mat4, 64> &bones,
               const std::array<bool, 64> &set, const RobotMover &mover, float seconds);
  [[nodiscard]] const std::array<float, 64> &angles() const {
    return angles_;
  }
  [[nodiscard]] const std::array<std::optional<Mat4>, 64> &frames() const {
    return frames_;
  }

private:
  std::array<float, 4> accumulators_{};
  std::array<float, 64> angles_{};
  Mat4 spherical_;
  std::array<std::optional<Mat4>, 64> frames_{};
  std::optional<Vec3> previous_position_;
};

} // namespace phantomcrash
