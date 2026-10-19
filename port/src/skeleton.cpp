#include "phantomcrash/skeleton.hpp"
#include "phantomcrash/weapon_mounts.hpp"
#include "phantomcrash/source_leg_playback.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace phantomcrash {
namespace {

constexpr float epsilon = 1e-5F;

Vec3 vec3_of(const json::JsonValue *array) {
  if (array == nullptr || !array->is_array() || array->array().size() != 3) {
    return {};
  }
  const auto &values = array->array();
  return {static_cast<float>(values[0].number()), static_cast<float>(values[1].number()),
          static_cast<float>(values[2].number())};
}

Vec3 row(const Mat4 &m, const int index) {
  return {m.at(index, 0), m.at(index, 1), m.at(index, 2)};
}

Mat4 frame(const Vec3 x, const Vec3 y, const Vec3 z, const Vec3 origin) {
  Mat4 m;
  const Vec3 rows[4] = {x, y, z, origin};
  for (int r = 0; r < 4; ++r) {
    m.at(r, 0) = rows[r].x;
    m.at(r, 1) = rows[r].y;
    m.at(r, 2) = rows[r].z;
  }
  return m;
}

// A bone frame along `direction` whose Z stays as close to `hinge` as it can.
Mat4 bone_frame(const Vec3 direction, const Vec3 hinge, const Vec3 fallback, const Vec3 origin) {
  const Vec3 x = normalize(direction);
  Vec3 z = hinge - x * dot(hinge, x);
  if (length(z) < epsilon) {
    z = fallback - x * dot(fallback, x);
  }
  z = normalize(z);
  return frame(x, cross(z, x), z, origin);
}

struct Transform {
  Vec3 rotation;
  Vec3 translation;
};

// The static values mirrored into game space, or the track's sample with the
// animation's own axis conventions (0x0ae6c0).
Transform node_transform(const SkeletonNode &node, const TrackSample &sample) {
  Transform t;
  const Vec3 r = node.rotation;
  const Vec3 p = node.translation;
  t.rotation = {-r.x, -r.y, r.z};
  t.translation = {p.x, p.y, -p.z};
  if (sample.rotation) {
    t.rotation = {-sample.rotation->x, sample.rotation->y, -sample.rotation->z};
  }
  if (sample.translation) {
    t.translation = {sample.translation->x, -sample.translation->y, sample.translation->z};
  }
  return t;
}

Mat4 local_matrix(const Transform &t) {
  return rotation_x(t.rotation.x) * rotation_y(t.rotation.y) * rotation_z(t.rotation.z) *
         translation(t.translation);
}

struct Context {
  const PoseInput &input;
  Pose &pose;
  int part{};
  const MotionSource *source{};
  Vec3 sway{}; // body motion moved onto the feet targets (0x9a6f50, 0x9a6f58)
};

void visit(const SkeletonNode &node, const Mat4 &parent, Context &context);

void store(Context &context, const SkeletonNode &node, const Mat4 &world) {
  if (node.source_model_root) {
    context.pose[static_cast<std::size_t>(context.part)].source_model_root = world;
  }
  if (context.part == 0 && context.pose[0].source_model_root && node.track >= 0) {
    auto &controls = context.pose[0].source_controls;
    const auto index = static_cast<std::size_t>(node.track);
    if (controls.size() <= index) {
      controls.resize(index + 1);
    }
    controls[index] = world;
  }
  // 0x0acd11 registers the high-flag attachment matrices, separately from feet.
  for (std::size_t slot = 0; slot < 14; ++slot) {
    if ((node.flags & (std::uint32_t{0x40000} << slot)) != 0) {
      context.pose[static_cast<std::size_t>(context.part)].movement_mounts[slot] = world;
    }
  }
  if ((context.part == 0 && (node.flags & node_turns_with_aim) != 0) ||
      (context.part > 0 && (node.flags & 0xc0) != 0)) {
    context.pose[static_cast<std::size_t>(context.part)].mount = world;
  }
  if (node.bone >= 0 && node.bone < bone_slots) {
    PosePart &part = context.pose[static_cast<std::size_t>(context.part)];
    part.bones[static_cast<std::size_t>(node.bone)] = world;
    part.set[static_cast<std::size_t>(node.bone)] = true;
  }
}

void visit_children(const SkeletonNode &node, const std::size_t first, const Mat4 &world,
                    Context &context) {
  for (std::size_t i = first; i < node.children.size(); ++i) {
    visit(node.children[i], world, context);
  }
}

Mat4 animated_transform(const SkeletonNode &node, Context &context) {
  if (const auto source = source_leg_transform(node, context.input, context.part)) {
    return *source;
  }
  TrackSample sample = node.track >= 0 ? context.source->sample(node.track) : TrackSample{};
  if (node.track < 0) {
    // 0x0ad51c samples track zero of a separate body-bank entry. These
    // channels replace the bind transform, not the locomotion track.
    constexpr std::uint32_t flags[] = {0x400, 0x800, 0, 0x2000, 0x4000, 0x8000, 0x10000};
    for (std::size_t slot = 0; slot < std::size(flags); ++slot) {
      if ((node.flags & flags[slot]) != 0) {
        sample = context.input.body_auxiliary[slot].sample(0);
        break;
      }
    }
  }
  Transform t = node_transform(node, sample);
  if (node.source_model_root) {
    // S.L.A.I. excludes root-key travel from the control matrix (0x19f480).
    // Gameplay moves the actor; applying the same travel here displaces the
    // visible assembly and its attachments again when a walk clip loops.
    t.translation = node_transform(node, {}).translation;
  }
  const Vec3 sampled_rotation = t.rotation;
  if (node.flags == node_root_motion && node.track >= 0) {
    // The walk's forward (and, outside idle and walking, sideways) body
    // motion moves onto the feet; the robot itself moves by the game. Each
    // frozen layer keeps the split it was posed with, so leaving a dodge
    // does not put its sideways offset back on the body.
    const Vec3 bind = node_transform(node, {}).translation;
    const RootSplit root =
        context.source->root_split(node.track, bind, t.translation, context.input.keep_sway);
    t.translation = root.body;
    context.sway.x = root.sway.x;
    context.sway.z = root.sway.z;
  }
  if ((node.flags & node_turns_with_aim) != 0) {
    t.rotation.y += context.input.aim_yaw;
  }
  if ((node.flags & node_wheel) != 0 && !(node.source_roll && node.source_roll->spherical)) {
    t.rotation.x += node.source_roll && context.input.source_roll_angles
                        ? (*context.input.source_roll_angles)[static_cast<std::size_t>(node.track)]
                        : context.input.wheel_turn;
  } else if ((node.flags & node_pitches_with_aim) != 0) {
    t.rotation.x += std::clamp(context.input.aim_pitch, -aim_pitch_limit, aim_pitch_limit);
  } else if ((node.flags & node_forearm) != 0) {
    // The body's type turns the forearms (0x0aa499: a Photon body's by -pi/2).
    t.rotation.x += context.input.forearm_turn;
  }
  const MotionSource &source = *context.source;
  if (node.track >= 0 && source.blending()) {
    // 0x0ac757 converts both rotations to quaternions before interpolation.
    const Vec3 adjustment = t.rotation - sampled_rotation;
    const auto as_matrix = [&](const MotionLayer &layer) {
      Transform out = node_transform(node, layer.motion->sample(node.track, layer.frame));
      out.rotation = out.rotation + adjustment;
      out.translation = t.translation;
      return local_matrix(out);
    };
    const auto mix = [](const Mat4 &a, const Mat4 &b, const float share) {
      return blend_pose_transform(a, b, share);
    };
    if (const auto from = source.blend_from<Mat4>(as_matrix, mix)) {
      const MotionLayer current{source.motion, source.frame};
      return blend_pose_transform(*from, as_matrix(current), source.weight);
    }
  }
  return local_matrix(t);
}

// Which way the knee bends: the side of the chain the bind pose puts it on.
float bend_side(const SkeletonNode &upper, const SkeletonNode &mid, const SkeletonNode &end) {
  const Mat4 upper_local = local_matrix(node_transform(upper, {}));
  const Mat4 mid_world = local_matrix(node_transform(mid, {})) * upper_local;
  const Vec3 knee = row(mid_world, 3);
  const Vec3 tip = transform_point({0, 0, 0}, local_matrix(node_transform(end, {})) * mid_world);
  return dot(cross(tip, knee), {0.0F, 0.0F, 1.0F}) < 0.0F ? -1.0F : 1.0F;
}

// A leg chain (0x0aa94b): the chain's first bone aims at the end node's
// track (one bone), or two bones bend to reach it, in the plane that keeps
// the chain's hinge (its Z axis) where the bind pose has it.
void visit_chain(const SkeletonNode &node, const Mat4 &parent, Context &context) {
  const Mat4 chain = animated_transform(node, context) * parent;
  if (node.children.empty() || node.children[0].children.empty()) {
    visit_children(node, 0, chain, context);
    return;
  }
  const SkeletonNode &upper = node.children[0];
  const SkeletonNode &mid = upper.children[0];
  const bool one_bone = mid.type == SkeletonNodeType::ik_end;
  if (!one_bone && mid.children.empty()) {
    visit_children(node, 0, chain, context);
    return;
  }
  const SkeletonNode &end = one_bone ? mid : mid.children[0];

  const TrackSample sample = context.source->sample(end.track);
  const Vec3 origin = row(chain, 3);
  const Vec3 hinge = row(chain, 2);
  const Vec3 fallback = row(chain, 1);
  Vec3 target = origin + row(chain, 0);
  if (sample.translation) {
    const Vec3 local{sample.translation->x + context.sway.x, -sample.translation->y,
                     sample.translation->z + context.sway.z};
    target = transform_point(local, context.input.root);
  }

  const float upper_length = length(mid.translation);
  Mat4 upper_world;
  Mat4 mid_world;
  Mat4 end_world;
  if (one_bone) {
    upper_world = bone_frame(target - origin, hinge, fallback, origin);
    mid_world = frame(row(upper_world, 0), row(upper_world, 1), row(upper_world, 2),
                      origin + row(upper_world, 0) * upper_length);
  } else {
    const float lower_length = length(end.translation);
    Vec3 to_target = target - origin;
    const float reach =
        std::clamp(length(to_target), std::fabs(upper_length - lower_length) + 1e-3F,
                   upper_length + lower_length - 1e-3F);
    const Vec3 d = length(to_target) > epsilon ? normalize(to_target) : row(chain, 0);
    Vec3 n = hinge - d * dot(hinge, d);
    n = length(n) > epsilon ? normalize(n) : normalize(fallback - d * dot(fallback, d));
    const float cos_root =
        (upper_length * upper_length + reach * reach - lower_length * lower_length) /
        (2.0F * upper_length * reach);
    const float root_angle = std::acos(std::clamp(cos_root, -1.0F, 1.0F));
    const float side = bend_side(upper, mid, end);
    const Vec3 u = d * std::cos(root_angle) + cross(n, d) * (std::sin(root_angle) * side);
    const Vec3 knee = origin + u * upper_length;
    const Vec3 tip = origin + d * reach;
    upper_world = frame(u, cross(n, u), n, origin);
    const Vec3 w = normalize(tip - knee);
    mid_world = frame(w, cross(n, w), n, knee);
    end_world = frame(w, cross(n, w), n, tip);
  }

  visit_children(node, 1, chain, context);
  store(context, upper, upper_world);
  visit_children(upper, 1, upper_world, context);
  store(context, mid, mid_world);
  visit_children(mid, one_bone ? 0 : 1, mid_world, context);
  if (!one_bone) {
    store(context, end, end_world);
    visit_children(end, 1, end_world, context);
  }
}

void visit(const SkeletonNode &node, const Mat4 &parent, Context &context) {
  if ((node.flags & node_effect_point) != 0) {
    context.pose[static_cast<std::size_t>(context.part)].contacts.push_back(
        pose_contact(node, parent, *context.source));
    return; // contact nodes draw nothing (0x0ad737)
  }
  if (node.type == SkeletonNodeType::ik_start) {
    visit_chain(node, parent, context);
    return;
  }
  Mat4 world = animated_transform(node, context) * parent;
  apply_source_spherical_roll(world, node, context.input.root, context.input.source_roll_frames);
  apply_source_suspension(world, node.source_suspension, context.input.procedural_seconds);
  if (context.part > 0 && node.track >= 0) {
    const auto side = static_cast<std::size_t>(context.part - 1);
    const auto &aim = context.input.arm_aim[side];
    const auto &source = context.input.firing_poses[side];
    if (aim.weight > 0 && source.motion != nullptr) {
      Transform firing = node_transform(node, source.sample(node.track));
      if ((node.flags & node_forearm) != 0) {
        firing.rotation.x += context.input.forearm_turn;
      }
      Mat4 raised = local_matrix(firing) * parent;
      if ((node.flags & node_forearm) != 0) {
        const Mat4 &root = context.input.root;
        raised =
            aim_pose_transform(raised, aim.target, {root.at(1, 0), root.at(1, 1), root.at(1, 2)});
      } else if ((node.flags & 0xc0) != 0) {
        const Vec3 delta = aim.target - transform_point({}, raised);
        const float pitch = std::atan2(delta.y, std::sqrt(delta.x * delta.x + delta.z * delta.z));
        firing.rotation.x += std::clamp(pitch, -aim.pitch_limit, aim.pitch_limit);
        raised = local_matrix(firing) * parent;
      }
      world = blend_pose_transform(world, raised, aim.weight);
    }
  }
  store(context, node, world);
  if (node.type == SkeletonNodeType::arms) {
    for (std::size_t side = 0; side < 2; ++side) {
      const SkeletonNode *arm = context.input.arm_skeletons[side];
      if (arm == nullptr) {
        continue;
      }
      Context arm_context{context.input, context.pose, static_cast<int>(side) + 1,
                          &context.input.arms[side], context.sway};
      visit(*arm, world, arm_context);
    }
  }
  visit_children(node, 0, world, context);
}

SkeletonNode parse_node(const json::JsonValue &value, const int depth) {
  if (depth > 64 || !value.is_object()) {
    throw std::runtime_error("Bad skeleton node");
  }
  SkeletonNode node;
  node.type = static_cast<SkeletonNodeType>(static_cast<int>(value.number_or("type", 0)));
  node.flags = static_cast<std::uint32_t>(value.number_or("flags", 0));
  node.track = static_cast<int>(value.number_or("track", -1));
  node.translation = vec3_of(value.find("translation"));
  node.rotation = vec3_of(value.find("rotation"));
  node.bone = static_cast<int>(value.number_or("bone", -1));
  node.source_model_root = value.boolean_or("source_model_root", false);
  node.source_suspension = parse_source_suspension(value);
  node.source_roll = parse_source_roll(value);
  if (const json::JsonValue *children = value.find("children"); children && children->is_array()) {
    for (const json::JsonValue &child : children->array()) {
      node.children.push_back(parse_node(child, depth + 1));
    }
  }
  return node;
}

std::vector<std::optional<SkeletonNode>> parse_list(const json::JsonValue *array) {
  std::vector<std::optional<SkeletonNode>> out;
  if (array == nullptr || !array->is_array()) {
    return out;
  }
  for (const json::JsonValue &value : array->array()) {
    if (value.is_null()) {
      out.emplace_back();
    } else {
      out.emplace_back(parse_node(value, 0));
    }
  }
  return out;
}

} // namespace

SkeletonNode parse_skeleton_node(const json::JsonValue &value) {
  return parse_node(value, 0);
}

RobotSkeletons parse_robot_skeletons(const json::JsonValue &document) {
  RobotSkeletons skeletons;
  skeletons.legs = parse_list(document.find("legs"));
  if (const json::JsonValue *arms = document.find("arms"); arms && arms->is_array()) {
    for (std::size_t side = 0; side < 2 && side < arms->array().size(); ++side) {
      skeletons.arms[side] = parse_list(&arms->array()[side]);
    }
  }
  return skeletons;
}

void build_pose(const SkeletonNode &root, const PoseInput &input, Pose &pose) {
  for (PosePart &part : pose) {
    part.set.fill(false);
    part.contacts.clear();
    part.mount.reset();
    part.source_model_root.reset();
    part.source_controls.clear();
    part.movement_mounts.fill(std::nullopt);
  }
  Context context{input, pose, 0, &input.legs, {}};
  visit(root, input.root, context);
}

} // namespace phantomcrash
