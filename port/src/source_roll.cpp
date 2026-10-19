#include "phantomcrash/source_roll.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/robot_mover.hpp"
#include "phantomcrash/skeleton.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace phantomcrash {
namespace {
void controls(const SkeletonNode &node, std::vector<const SkeletonNode *> &out) {
  if (node.source_roll) {
    out.push_back(&node);
  }
  for (const auto &child : node.children) {
    controls(child, out);
  }
}

float wrap(float angle, const float half_turn) {
  while (angle > half_turn) {
    angle -= 2 * half_turn;
  }
  while (angle < -half_turn) {
    angle += 2 * half_turn;
  }
  return angle;
}
} // namespace

bool has_source_roll(const SkeletonNode &node) {
  return node.source_roll ||
         std::any_of(node.children.begin(), node.children.end(), has_source_roll);
}

std::optional<SourceRollControl> parse_source_roll(const json::JsonValue &node) {
  const auto *value = node.find("source_roll");
  if (!value) {
    return std::nullopt;
  }
  const auto read = [&](const char *field) {
    return static_cast<float>(json::required(*value, field).number());
  };
  SourceRollControl out{static_cast<int>(read("group")), read("coefficient"), read("pi"),
                        read("max_step"), read("source_units_per_native")};
  out.spherical = value->boolean_or("spherical", false);
  out.steering_radians_per_degree = read("steering_radians_per_degree");
  out.native_clock_rate = read("native_clock_rate");
  if (out.spherical) {
    const auto &scale = json::required(*value, "baked_mesh_scale").array();
    if (scale.size() != 3) {
      throw std::runtime_error("Invalid source spherical mesh scale");
    }
    out.baked_mesh_scale = {static_cast<float>(scale[0].number()),
                            static_cast<float>(scale[1].number()),
                            static_cast<float>(scale[2].number())};
    for (const auto component :
         {out.baked_mesh_scale.x, out.baked_mesh_scale.y, out.baked_mesh_scale.z}) {
      if (!(component > 0) || !std::isfinite(component)) {
        throw std::runtime_error("Invalid source spherical mesh scale");
      }
    }
  }
  const auto track = node.number_or("track", -1);
  const auto bone = node.number_or("bone", -1);
  const auto *children = node.find("children");
  if (out.group < 0 || out.group >= 4 || !std::isfinite(out.coefficient) ||
      !std::isfinite(out.pi) || out.pi <= 0 || !std::isfinite(out.max_step) || out.max_step <= 0 ||
      !std::isfinite(out.source_units_per_native) || out.source_units_per_native <= 0 ||
      !std::isfinite(out.steering_radians_per_degree) || out.steering_radians_per_degree <= 0 ||
      !std::isfinite(out.native_clock_rate) || out.native_clock_rate <= 0 || track < 0 ||
      track >= 64 || bone < 0 || bone >= 64 || (children && !children->array().empty())) {
    throw std::runtime_error("Invalid source rolling leaf control");
  }
  return out;
}

void apply_source_spherical_roll(Mat4 &world, const SkeletonNode &node, const Mat4 &model_root,
                                 const std::array<std::optional<Mat4>, 64> *frames) {
  if (!node.source_roll || !node.source_roll->spherical || !frames) {
    return;
  }
  const auto &frame = frames->at(static_cast<std::size_t>(node.track));
  if (!frame) {
    return;
  }
  const auto &scale = node.source_roll->baked_mesh_scale;
  const Mat4 basis = scaling({1 / scale.x, 1 / scale.y, 1 / scale.z}) * *frame * model_root;
  // 0x4e5a30..90 replaces the local basis, then restores the old world position.
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      world.at(row, column) = basis.at(row, column);
    }
  }
}

void SourceRollState::advance(const SkeletonNode &rig, const std::array<Mat4, 64> &bones,
                              const std::array<bool, 64> &set, const RobotMover &mover,
                              const float seconds) {
  std::vector<const SkeletonNode *> nodes;
  controls(rig, nodes);
  if (nodes.empty() || seconds <= 0) {
    return;
  }
  const auto position = mover.position();
  const float heading = mover.heading();
  if (!previous_position_) {
    previous_position_ = position;
  }
  const Vec3 travel = position - *previous_position_;
  const Mat4 inverse_heading = rotation_y(-heading);
  const float local_z = transform_point(travel, inverse_heading).z;
  const float lateral = transform_point(travel, inverse_heading).x;
  previous_position_ = position;
  if (mover.mode() == MoveMode::destroyed) {
    return;
  }
  // Descriptor registration is ascending by source node. Shared accumulators
  // advance for each callback, so adjacent meshes can have different angles.
  std::sort(nodes.begin(), nodes.end(),
            [](const auto *a, const auto *b) { return a->track < b->track; });
  for (const auto *node : nodes) {
    const auto &c = *node->source_roll;
    const auto bone = static_cast<std::size_t>(node->bone);
    if (!set[bone]) {
      throw std::runtime_error("Missing source rolling control pose");
    }
    const float denominator = c.coefficient > 0 ? c.pi * c.coefficient : c.pi / 2;
    const float clock_ticks = seconds * c.native_clock_rate;
    // 0x4e55fc..5614 uses steering input, turn speed and elapsed clock ticks.
    // Native controls/load stats supply the actor inputs; yaw displacement
    // (including teleports, reverse steering and quick turns) is not an input.
    const float turn_per_clock = -mover.rolling_steer() *
                                 (mover.tuning().turn_degrees_per_second / c.native_clock_rate) *
                                 c.steering_radians_per_degree;
    float step;
    if (mover.grounded()) {
      const Vec3 wheel_position = transform_point({}, bones[bone]);
      const float local_x = transform_point(wheel_position - position, inverse_heading).x;
      step = local_z * c.source_units_per_native / denominator;
      if (!c.spherical) {
        const float turn = turn_per_clock * clock_ticks;
        step += local_x > 0 ? turn : -turn;
      }
    } else {
      // Actor +0xe10 is the forward control; native tick duration supplies cadence.
      step = -mover.stick()[1] * seconds * battle_tick_rate / denominator;
    }
    step = std::clamp(step, -c.max_step, c.max_step);
    auto &accumulator = accumulators_[static_cast<std::size_t>(c.group)];
    accumulator = wrap(accumulator + step, c.pi);
    angles_[static_cast<std::size_t>(node->track)] = accumulator;
    if (c.spherical) {
      const float sideways = mover.grounded()
                                 ? std::clamp(-lateral * c.source_units_per_native / denominator,
                                              -c.max_step, c.max_step)
                                 : 0;
      accumulators_[1] = wrap(accumulators_[1] - sideways, c.pi);
      // 0x4e5a00..2c updates the persistent actor matrix X, Y, then Z.
      // 0x4e56f4..56fc divides steering by clock ticks for this axis.
      // Y/Z signs reflect source axes; elapsed ticks do not scale Z rotation.
      spherical_ =
          spherical_ * rotation_x(step) * rotation_y(sideways) * rotation_z(turn_per_clock);
      frames_[static_cast<std::size_t>(node->track)] = spherical_;
    }
  }
}
} // namespace phantomcrash
