#include "desktop_robot.hpp"
#include "phantomcrash/rigid_animation.hpp"
#include <algorithm>

namespace phantomcrash {
namespace {

int flagged_bone(const SkeletonNode &node, const std::uint32_t flag) {
  if ((node.flags & flag) != 0 && node.bone >= 0) {
    return node.bone;
  }
  for (const SkeletonNode &child : node.children) {
    if (const int bone = flagged_bone(child, flag); bone >= 0) {
      return bone;
    }
  }
  return -1;
}

int load(WorldRenderer &world, const std::string &path) {
  return path.empty() ? -1 : world.model(path);
}

} // namespace

RobotVisual::RobotVisual(WorldRenderer &world, const RobotAssembly &assembly) : world_(world) {
  source_turn_frames_ = assembly.source_turn_frames;
  body_ = load(world, assembly.body_model);
  legs_ = load(world, assembly.legs_model);
  paint_ = world.texture(assembly.paint);
  emblem_ = load(world, assembly.emblem_model);
  emblem_texture_ = world.texture(assembly.emblem_image);
  if (assembly.skeleton != nullptr) {
    torso_slot_ = flagged_bone(*assembly.skeleton, node_turns_with_aim);
  }
  const ModelData *body = world.model_data(body_);
  int body_bones = 0;
  if (body != nullptr) {
    for (std::size_t i = 0; i < body->nodes.size(); ++i) {
      if (std::any_of(body->draws.begin(), body->draws.end(),
                      [i](const ModelDraw &draw) { return draw.node == static_cast<int>(i); })) {
        ++body_bones;
      }
    }
  }
  if (body && body->rig) {
    body_bones = 1;
    for (std::size_t side = 0; side < 2; ++side) {
      source_arm_mount_valid_[side] = body->rig->arm_mount_nodes[side] >= 0;
      source_shoulder_mount_valid_[side] = body->rig->shoulder_mount_nodes[side] >= 0;
    }
  }
  parts_.push_back({body_, 0, 0, {}});
  parts_.push_back({legs_, 0, body_bones, {}});
  parts_[0].paint = world.texture(assembly.part_paints[0]);
  parts_[1].paint = world.texture(assembly.part_paints[1]);
  for (std::size_t side = 0; side < 2; ++side) {
    parts_.push_back({load(world, assembly.arm_models[side]), static_cast<int>(side) + 1, 0, {}});
    shoulders_.push_back({load(world, assembly.shoulder_models[side]), 0, 0, {}});
    parts_.back().paint = world.texture(assembly.part_paints[side + 2]);
    shoulders_.back().paint = world.texture(assembly.part_paints[side + 4]);
    shoulder_shifts_[side] = translation(assembly.shoulder_shifts[side]);
  }
  for (const std::vector<Part> *parts : {&parts_, &shoulders_}) {
    for (const Part &part : *parts) {
      world.prepare_shadow(part.model);
    }
  }
}

void RobotVisual::add(Part &part, const Pose &pose, const Mat4 &root, std::vector<WorldItem> &items,
                      const int weapon_slot) {
  const ModelData *model = world_.model_data(part.model);
  if (model == nullptr) {
    return;
  }
  const auto &p = pose[static_cast<std::size_t>(part.pose_part)];
  if (model->rig) {
    const Mat4 mount = weapon_slot >= 0 && weapon_slot < 2 && source_arm_mounts_ &&
                               source_arm_mount_valid_[static_cast<std::size_t>(weapon_slot)]
                           ? (*source_arm_mounts_)[static_cast<std::size_t>(weapon_slot)]
                       : p.set[0] ? p.bones[0]
                                  : root;
    rigid(*model, mount, weapon_slot, part.node_worlds);
  } else {
    skin_nodes(*model, p, part.first_slot, root, part.node_worlds);
  }
  WorldItem item;
  item.model = part.model;
  item.world = root;
  item.node_worlds = part.node_worlds;
  item.paint = paint_;
  item.part_paint = part.paint;
  items.push_back(std::move(item));
}

void RobotVisual::add_items(const Pose &pose, const Mat4 &root, std::vector<WorldItem> &items,
                            const std::array<Mat4, 2> *shoulder_mounts,
                            const std::array<bool, 4> &lost) {
  for (std::size_t i = 0; i < parts_.size(); ++i) {
    if (i < 2 || !lost[i - 2]) {
      add(parts_[i], pose, root, items, i >= 2 ? static_cast<int>(i) - 2 : -1);
    }
  }
  const PosePart &body = pose[0];
  const bool torso = torso_slot_ >= 0 && body.set[static_cast<std::size_t>(torso_slot_)];
  const Mat4 mount = torso ? body.bones[static_cast<std::size_t>(torso_slot_)] : root;
  if (emblem_ >= 0 && emblem_texture_ >= 0) {
    WorldItem decal;
    decal.model = emblem_;
    decal.world = mount;
    decal.paint = emblem_texture_;
    decal.part_paint = emblem_texture_;
    items.push_back(std::move(decal));
  }
  for (std::size_t side = 0; side < shoulders_.size(); ++side) {
    Part &part = shoulders_[side];
    if (lost[side + 2]) {
      continue;
    }
    const ModelData *model = world_.model_data(part.model);
    if (model == nullptr) {
      continue;
    }
    WorldItem item;
    item.model = part.model;
    // A battle's mounts already carry the shift (RobotCombat).
    item.world = shoulder_mounts ? (*shoulder_mounts)[side] : shoulder_shifts_[side] * mount;
    if (model->rig && source_shoulder_mounts_ && source_shoulder_mount_valid_[side]) {
      item.world =
          translation(model->rig->body_mount_offset * -1.0F) * (*source_shoulder_mounts_)[side];
    }
    if (model->rig) {
      rigid(*model, item.world, static_cast<int>(side) + 2, item.node_worlds);
    }
    item.paint = paint_;
    item.part_paint = part.paint;
    items.push_back(std::move(item));
  }
}

void RobotVisual::animate(const int motion, const float frame, const bool loop, const float seconds,
                          const std::array<bool, 4> &fired, const std::uint64_t revision,
                          const float speed) {
  motion_ = motion;
  frame_ = frame;
  loop_ = loop;
  if (const auto *body = world_.model_data(body_);
      body && body->rig && body->rig->preserve_disabled_clips) {
    const int turn =
        motion == 8 || motion == 9 ? source_turn_frames_[source_actor_idle_ ? 0 : 1] : -1;
    body_playback_.update(*body->rig, motion, frame, loop, seconds, revision, turn, speed);
  }
  if (motion < 37 || motion > 44) {
    source_actor_idle_ = motion == 28;
  }
  for (std::size_t slot = 0; slot < fired.size(); ++slot) {
    weapon_seconds_[slot] = fired[slot] ? 0 : weapon_seconds_[slot] + seconds;
    weapon_started_[slot] = weapon_started_[slot] || fired[slot];
    const int model = slot < 2 ? parts_[slot + 2].model : shoulders_[slot - 2].model;
    if (const auto *weapon = world_.model_data(model); weapon && weapon->rig) {
      weapon_playback_[slot].update_weapon(*weapon->rig, seconds, weapon_started_[slot],
                                           fired[slot]);
    }
  }
}

void RobotVisual::rigid(const ModelData &model, const Mat4 &root, const int weapon_slot,
                        std::vector<Mat4> &nodes, const int attachment_node,
                        Mat4 *attachment_frame) const {
  const auto &rig = *model.rig;
  std::string clip;
  float seconds = frame_ / rig.frame_rate;
  bool loop = loop_;
  if (weapon_slot >= 0) {
    const auto slot = static_cast<std::size_t>(weapon_slot);
    if (weapon_started_[slot]) {
      clip = rig.weapon_clip;
    }
    seconds = weapon_seconds_[slot];
    loop = false;
  } else if (rig.preserve_disabled_clips) {
    clip = body_playback_.clip;
    seconds = body_playback_.seconds;
    loop = body_playback_.loop;
  } else if (motion_ >= 0 && motion_ < static_cast<int>(rig.motion_clips.size())) {
    clip = rig.motion_clips[static_cast<std::size_t>(motion_)];
  }
  std::array<Mat4, 2> *mounts = nullptr;
  std::array<Mat4, 2> *shoulders = nullptr;
  if (weapon_slot < 0 && (rig.arm_mount_nodes[0] >= 0 || rig.arm_mount_nodes[1] >= 0)) {
    source_arm_mounts_.emplace();
    mounts = &*source_arm_mounts_;
  }
  if (weapon_slot < 0 && (rig.shoulder_mount_nodes[0] >= 0 || rig.shoulder_mount_nodes[1] >= 0)) {
    source_shoulder_mounts_.emplace();
    shoulders = &*source_shoulder_mounts_;
  }
  pose_rigid_model(
      model, clip, seconds, loop, root, nodes, mounts, attachment_node, attachment_frame, shoulders,
      weapon_slot >= 0 && !weapon_playback_[static_cast<std::size_t>(weapon_slot)].nodes.empty()
          ? &weapon_playback_[static_cast<std::size_t>(weapon_slot)].nodes
      : weapon_slot < 0 && rig.preserve_disabled_clips && !body_playback_.nodes.empty()
          ? &body_playback_.nodes
          : nullptr);
}

std::optional<Mat4> RobotVisual::source_weapon_frame(const Pose &pose, const Mat4 &root,
                                                     const std::array<Mat4, 2> &shoulder_mounts,
                                                     const int slot, const int node) const {
  if (slot < 0 || slot >= 4 || node < 0) {
    return std::nullopt;
  }
  const auto &part = slot < 2 ? parts_.at(static_cast<std::size_t>(slot) + 2)
                              : shoulders_.at(static_cast<std::size_t>(slot) - 2);
  const auto *model = world_.model_data(part.model);
  if (!model || !model->rig || node >= static_cast<int>(model->rig->nodes.size())) {
    return std::nullopt;
  }
  Mat4 mount = root;
  std::vector<Mat4> unused;
  const auto *body = world_.model_data(body_);
  if (body && body->rig) {
    const Mat4 body_root = pose[0].set[0] ? pose[0].bones[0] : root;
    rigid(*body, body_root, -1, unused);
  }
  if (slot < 2 && source_arm_mounts_ && source_arm_mount_valid_[static_cast<std::size_t>(slot)]) {
    mount = (*source_arm_mounts_)[static_cast<std::size_t>(slot)];
  } else if (slot >= 2) {
    const auto side = static_cast<std::size_t>(slot) - 2;
    mount =
        source_shoulder_mounts_ && source_shoulder_mount_valid_[side]
            ? translation(model->rig->body_mount_offset * -1.0F) * (*source_shoulder_mounts_)[side]
            : shoulder_mounts[side];
  } else if (pose[static_cast<std::size_t>(slot) + 1].set[0]) {
    mount = pose[static_cast<std::size_t>(slot) + 1].bones[0];
  }
  Mat4 frame;
  rigid(*model, mount, slot, unused, node, &frame);
  return frame;
}

std::optional<Mat4> RobotVisual::source_body_frame(const Pose &pose, const Mat4 &root,
                                                   const int node) const {
  const auto *body = world_.model_data(body_);
  if (!body || !body->rig || node < 0 || node >= static_cast<int>(body->rig->nodes.size())) {
    return std::nullopt;
  }
  const Mat4 mount = pose[0].set[0] ? pose[0].bones[0] : root;
  std::vector<Mat4> unused;
  Mat4 frame;
  rigid(*body, mount, -1, unused, node, &frame);
  return frame;
}

std::optional<Vec3> RobotVisual::torso(const Pose &pose) const {
  const PosePart &body = pose[0];
  if (torso_slot_ < 0 || !body.set[static_cast<std::size_t>(torso_slot_)]) {
    return std::nullopt;
  }
  const Mat4 &bone = body.bones[static_cast<std::size_t>(torso_slot_)];
  return Vec3{bone.at(3, 0), bone.at(3, 1), bone.at(3, 2)};
}

} // namespace phantomcrash
