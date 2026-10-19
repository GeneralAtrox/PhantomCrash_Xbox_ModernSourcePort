#include "phantomcrash/rigid_animation.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/world_model.hpp"
#include <cgltf.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>

namespace phantomcrash {
namespace {
Mat4 mirror(const float *values) {
  Mat4 result;
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      result.at(r, c) = values[r * 4 + c] * (r == 2 ? -1.0F : 1.0F) * (c == 2 ? -1.0F : 1.0F);
    }
  }
  return result;
}

Mat4 transform(const RigidNode &node) {
  const auto &q = node.rotation;
  const float x = q[0], y = q[1], z = q[2], w = q[3];
  Mat4 result;
  result.at(0, 0) = 1 - 2 * (y * y + z * z);
  result.at(0, 1) = 2 * (x * y + z * w);
  result.at(0, 2) = 2 * (x * z - y * w);
  result.at(1, 0) = 2 * (x * y - z * w);
  result.at(1, 1) = 1 - 2 * (x * x + z * z);
  result.at(1, 2) = 2 * (y * z + x * w);
  result.at(2, 0) = 2 * (x * z + y * w);
  result.at(2, 1) = 2 * (y * z - x * w);
  result.at(2, 2) = 1 - 2 * (x * x + y * y);
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      result.at(r, c) *= node.scale[static_cast<std::size_t>(r)];
    }
    result.at(3, r) = node.position[static_cast<std::size_t>(r)];
  }
  return result;
}

std::array<float, 4> interpolate(std::array<float, 4> a, const std::array<float, 4> &b,
                                 const float t, const bool rotation) {
  float dot = 0;
  for (int i = 0; i < 4; ++i) {
    dot += a[i] * b[i];
  }
  float wa = 1 - t, wb = t;
  if (rotation) {
    if (dot < 0) {
      dot = -dot;
      for (auto &value : a) {
        value = -value;
      }
    }
    if (dot < 0.9995F) {
      const float angle = std::acos(std::clamp(dot, -1.0F, 1.0F));
      wa = std::sin((1 - t) * angle) / std::sin(angle);
      wb = std::sin(t * angle) / std::sin(angle);
    }
  }
  std::array<float, 4> result{};
  float length = 0;
  for (int i = 0; i < 4; ++i) {
    result[i] = a[i] * wa + b[i] * wb;
    length += result[i] * result[i];
  }
  if (rotation && length > 0) {
    for (auto &value : result) {
      value /= std::sqrt(length);
    }
  }
  return result;
}
std::array<float, 4> sample(const RigidChannel &channel, const float time) {
  const auto upper = std::upper_bound(channel.times.begin(), channel.times.end(), time);
  const auto last = static_cast<std::size_t>(upper - channel.times.begin());
  if (last == 0) {
    return channel.values.front();
  }
  if (last == channel.times.size() || channel.step) {
    return channel.values[last - 1];
  }
  const float t =
      (time - channel.times[last - 1]) / (channel.times[last] - channel.times[last - 1]);
  return interpolate(channel.values[last - 1], channel.values[last], t,
                     channel.path == RigidChannel::Path::rotation);
}
} // namespace

void read_rigid_animation(const cgltf_data &data, ModelData &model, std::string prefix) {
  auto rig = std::make_shared<RigidRig>();
  rig->prefix = std::move(prefix);
  if (data.asset.extras.end_offset > data.asset.extras.start_offset) {
    const auto meta = json::parse(
        std::string_view(data.json + data.asset.extras.start_offset,
                         data.asset.extras.end_offset - data.asset.extras.start_offset));
    if (const auto *clips = meta.find("motion_clips")) {
      for (const auto &clip : clips->array()) {
        rig->motion_clips.push_back(clip.string());
      }
    }
    rig->weapon_clip = meta.string_or("weapon_clip", "");
    rig->preserve_disabled_clips = meta.boolean_or("preserve_disabled_clips", false);
    if (const auto *motions = meta.find("independent_motions")) {
      for (const auto &motion : motions->array()) {
        rig->independent_motions.push_back(static_cast<int>(motion.number()));
      }
    }
    if (const auto *frames = meta.find("source_transition_frames")) {
      for (const auto &frame : frames->array()) {
        rig->transition_frames.push_back(static_cast<int>(frame.number()));
      }
    }
    rig->frame_rate = static_cast<float>(meta.number_or("source_frame_rate", 60));
    if (const auto *mounts = meta.find("arm_mount_nodes")) {
      for (std::size_t side = 0; side < 2; ++side) {
        rig->arm_mount_nodes[side] = static_cast<int>(mounts->array().at(side).number());
      }
    }
    if (const auto *mounts = meta.find("shoulder_mount_nodes")) {
      for (std::size_t side = 0; side < 2; ++side) {
        rig->shoulder_mount_nodes[side] = static_cast<int>(mounts->array().at(side).number());
      }
    }
    if (const auto *offset = meta.find("body_mount_offset")) {
      const auto &xyz = offset->array();
      rig->body_mount_offset = {static_cast<float>(xyz.at(0).number()),
                                static_cast<float>(xyz.at(1).number()),
                                -static_cast<float>(xyz.at(2).number())};
    }
  }
  for (cgltf_size i = 0; i < data.nodes_count; ++i) {
    const auto &source = data.nodes[i];
    float local[16];
    cgltf_node_transform_local(&source, local);
    RigidNode node;
    node.local = mirror(local);
    node.parent = source.parent ? static_cast<int>(source.parent - data.nodes) : -1;
    std::copy_n(source.translation, 3, node.position.begin());
    node.position[2] = -node.position[2];
    std::copy_n(source.rotation, 4, node.rotation.begin());
    node.rotation[0] = -node.rotation[0];
    node.rotation[1] = -node.rotation[1];
    std::copy_n(source.scale, 3, node.scale.begin());
    rig->nodes.push_back(node);
  }
  for (cgltf_size i = 0; i < data.animations_count; ++i) {
    const auto &source = data.animations[i];
    RigidClip clip;
    clip.name = source.name ? source.name : "";
    if (source.extras.end_offset > source.extras.start_offset) {
      const auto extras =
          json::parse(std::string_view(data.json + source.extras.start_offset,
                                       source.extras.end_offset - source.extras.start_offset));
      if (const auto *tracks = extras.find("source_pmt")) {
        for (const auto &track : tracks->array()) {
          clip.source_tracks.push_back(parse_source_pmt(track));
        }
        if (clip.source_tracks.size() > rig->nodes.size()) {
          throw std::runtime_error("Rigid source motion exceeds its hierarchy");
        }
      }
    }
    for (cgltf_size j = 0; j < source.channels_count; ++j) {
      const auto &channel = source.channels[j];
      if (!channel.target_node || !channel.sampler) {
        throw std::runtime_error("Rigid animation lacks a target or sampler");
      }
      const auto &sampler = *channel.sampler;
      if (sampler.interpolation == cgltf_interpolation_type_cubic_spline) {
        throw std::runtime_error("Rigid cubic animation is unsupported");
      }
      RigidChannel out;
      out.node = static_cast<int>(channel.target_node - data.nodes);
      out.step = sampler.interpolation == cgltf_interpolation_type_step;
      const bool rotation = channel.target_path == cgltf_animation_path_type_rotation;
      out.path = rotation ? RigidChannel::Path::rotation
                 : channel.target_path == cgltf_animation_path_type_translation
                     ? RigidChannel::Path::position
                     : RigidChannel::Path::scale;
      if (channel.target_path == cgltf_animation_path_type_weights) {
        throw std::runtime_error("Rigid morph animation is unsupported");
      }
      for (cgltf_size key = 0; key < sampler.input->count; ++key) {
        float time{};
        std::array<float, 4> value{};
        if (!cgltf_accessor_read_float(sampler.input, key, &time, 1) ||
            !cgltf_accessor_read_float(sampler.output, key, value.data(), rotation ? 4 : 3)) {
          throw std::runtime_error("Rigid animation accessor is invalid");
        }
        if (rotation) {
          value[0] = -value[0];
          value[1] = -value[1];
        } else if (out.path == RigidChannel::Path::position) {
          value[2] = -value[2];
        }
        out.times.push_back(time);
        out.values.push_back(value);
        clip.duration = std::max(clip.duration, time);
      }
      if (!out.times.empty()) {
        clip.channels.push_back(std::move(out));
      }
    }
    rig->clips.push_back(std::move(clip));
  }
  model.rig = std::move(rig);
}

std::vector<RigidNode> sample_rigid_pose(const RigidRig &rig, const std::string_view name,
                                         float time, const bool loop) {
  auto nodes = rig.nodes;
  std::vector<bool> changed(nodes.size());
  const auto clip = std::find_if(rig.clips.begin(), rig.clips.end(),
                                 [&](const auto &c) { return c.name == name; });
  if (clip != rig.clips.end()) {
    time = loop && clip->duration > 0 ? std::fmod(std::max(time, 0.0F), clip->duration)
                                      : std::clamp(time, 0.0F, clip->duration);
    for (const auto &channel : clip->channels) {
      auto &node = nodes.at(static_cast<std::size_t>(channel.node));
      const auto value = sample(channel, time);
      if (channel.path == RigidChannel::Path::position) {
        std::copy_n(value.begin(), 3, node.position.begin());
      } else if (channel.path == RigidChannel::Path::scale) {
        std::copy_n(value.begin(), 3, node.scale.begin());
      } else {
        node.rotation = value;
      }
      changed[static_cast<std::size_t>(channel.node)] = true;
    }
  }
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    if (changed[index]) {
      nodes[index].local = transform(nodes[index]);
    }
  }
  return nodes;
}

void blend_rigid_pose(std::vector<RigidNode> &from, const std::vector<RigidNode> &to,
                      const float weight) {
  if (from.size() != to.size()) {
    throw std::runtime_error("Rigid transition has a different control hierarchy");
  }
  for (std::size_t index = 0; index < from.size(); ++index) {
    auto &a = from[index];
    const auto &b = to[index];
    if (a.local.m == b.local.m) {
      continue;
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
      a.position[axis] += (b.position[axis] - a.position[axis]) * weight;
      a.scale[axis] += (b.scale[axis] - a.scale[axis]) * weight;
    }
    a.rotation = interpolate(a.rotation, b.rotation, weight, true);
    a.local = transform(a);
  }
}

void pose_rigid_model(const ModelData &model, const std::string_view name, const float time,
                      const bool loop, const Mat4 &root, std::vector<Mat4> &out,
                      std::array<Mat4, 2> *arm_mounts, const int attachment_node,
                      Mat4 *attachment_frame, std::array<Mat4, 2> *shoulder_mounts,
                      const std::vector<RigidNode> *evaluated) {
  if (!model.rig) {
    throw std::runtime_error("Model has no rigid animation rig");
  }
  const auto &rig = *model.rig;
  const auto sampled =
      evaluated ? std::vector<RigidNode>{} : sample_rigid_pose(rig, name, time, loop);
  const auto &nodes = evaluated ? *evaluated : sampled;
  if (nodes.size() != rig.nodes.size()) {
    throw std::runtime_error("Rigid evaluated pose has a different control hierarchy");
  }
  std::vector<Mat4> worlds(nodes.size());
  std::vector<bool> ready(nodes.size());
  std::function<const Mat4 &(std::size_t)> visit = [&](const std::size_t index) -> const Mat4 & {
    if (!ready[index]) {
      const auto &node = nodes[index];
      worlds[index] =
          node.local * (node.parent >= 0 ? visit(static_cast<std::size_t>(node.parent)) : root);
      ready[index] = true;
    }
    return worlds[index];
  };
  out.clear();
  for (const auto &node : model.nodes) {
    out.push_back(visit(static_cast<std::size_t>(node.rig_node)));
  }
  const auto mounts = [&](const std::array<int, 2> &indices, std::array<Mat4, 2> *frames) {
    if (!frames) {
      return;
    }
    for (std::size_t side = 0; side < 2; ++side) {
      const int node = indices[side];
      if (node == -1) {
        (*frames)[side] = root;
      } else {
        if (node < 0 || node >= static_cast<int>(nodes.size())) {
          throw std::runtime_error("Source body attachment is outside its hierarchy");
        }
        (*frames)[side] = visit(static_cast<std::size_t>(node));
      }
    }
  };
  mounts(rig.arm_mount_nodes, arm_mounts);
  mounts(rig.shoulder_mount_nodes, shoulder_mounts);
  if (attachment_frame) {
    if (attachment_node < 0 || attachment_node >= static_cast<int>(nodes.size())) {
      throw std::runtime_error("Source attachment node is outside its hierarchy");
    }
    *attachment_frame = visit(static_cast<std::size_t>(attachment_node));
  }
}
} // namespace phantomcrash
