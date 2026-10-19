#include "phantomcrash/rigid_source_playback.hpp"
#include "phantomcrash/rigid_animation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace phantomcrash {
namespace {
void set_pose(RigidNode &node, const Mat4 &local) {
  node.local = local;
  Mat4 basis = local;
  for (int row = 0; row < 3; ++row) {
    node.position[static_cast<std::size_t>(row)] = local.at(3, row);
    const Vec3 axis{local.at(row, 0), local.at(row, 1), local.at(row, 2)};
    node.scale[static_cast<std::size_t>(row)] = length(axis);
  }
  const Vec3 x{local.at(0, 0), local.at(0, 1), local.at(0, 2)};
  const Vec3 y{local.at(1, 0), local.at(1, 1), local.at(1, 2)};
  const Vec3 z{local.at(2, 0), local.at(2, 1), local.at(2, 2)};
  if (dot(x, cross(y, z)) < 0) {
    node.scale[0] = -node.scale[0];
  }
  for (int row = 0; row < 3; ++row) {
    const float scale = node.scale[static_cast<std::size_t>(row)];
    if (std::abs(scale) < 1e-6F) {
      throw std::runtime_error("Singular rigid source control pose");
    }
    for (int column = 0; column < 3; ++column) {
      basis.at(row, column) /= scale;
    }
  }
  auto &q = node.rotation;
  const float trace = basis.at(0, 0) + basis.at(1, 1) + basis.at(2, 2);
  if (trace > 0) {
    const float s = std::sqrt(trace + 1) * 2;
    q = {(basis.at(1, 2) - basis.at(2, 1)) / s, (basis.at(2, 0) - basis.at(0, 2)) / s,
         (basis.at(0, 1) - basis.at(1, 0)) / s, s * .25F};
  } else {
    int i = 0;
    for (int axis = 1; axis < 3; ++axis) {
      if (basis.at(axis, axis) > basis.at(i, i)) {
        i = axis;
      }
    }
    const int j = (i + 1) % 3, k = (i + 2) % 3;
    const float s =
        std::sqrt(std::max(0.0F, 1 + basis.at(i, i) - basis.at(j, j) - basis.at(k, k))) * 2;
    q[i] = s * .25F;
    q[j] = (basis.at(i, j) + basis.at(j, i)) / s;
    q[k] = (basis.at(i, k) + basis.at(k, i)) / s;
    q[3] = (basis.at(j, k) - basis.at(k, j)) / s;
  }
}
} // namespace

void RigidSourcePlayback::start(const RigidClip &clip, const float frame, const bool loop) {
  clip_ = clip.source_tracks.empty() ? nullptr : &clip;
  cursors_.assign(clip.source_tracks.size(), {});
  for (std::size_t control = 0; control < cursors_.size(); ++control) {
    cursors_[control].reset(clip.source_tracks[control], frame, loop);
  }
}

std::vector<RigidNode> RigidSourcePlayback::sample(const RigidRig &rig) const {
  auto nodes = rig.nodes;
  if (clip_) {
    for (std::size_t control = 0; control < cursors_.size(); ++control) {
      set_pose(nodes.at(control), cursors_[control].sample(clip_->source_tracks[control]));
    }
  }
  return nodes;
}

void RigidSourcePlayback::advance(const float frames, const bool loop) {
  if (clip_) {
    for (std::size_t control = 0; control < cursors_.size(); ++control) {
      cursors_[control].advance(clip_->source_tracks[control], frames, loop);
    }
  }
}
} // namespace phantomcrash
