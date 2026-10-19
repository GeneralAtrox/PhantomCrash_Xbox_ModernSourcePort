#include "phantomcrash/skeleton.hpp"
#include <algorithm>

namespace phantomcrash {
PoseContact pose_contact(const SkeletonNode &node, const Mat4 &parent, const MotionSource &source) {
  PoseContact contact;
  contact.position =
      transform_point({node.translation.x, node.translation.y, -node.translation.z}, parent);
  contact.motion = source.motion;
  if (source.motion && node.track >= 0 &&
      node.track < static_cast<int>(source.motion->tracks.size())) {
    const auto &track = source.motion->tracks[static_cast<std::size_t>(node.track)];
    // 0x0ad737 uses the scale-channel key cursors as contact events, not scales.
    if (track.scale) {
      for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto &keys = (*track.scale)[axis].keys;
        contact.keys[axis] = static_cast<std::size_t>(
            std::lower_bound(keys.begin(), keys.end(), source.frame) - keys.begin());
      }
    }
  }
  return contact;
}

bool contact_stepped(const PoseContact &before, const PoseContact &now) {
  if (now.motion != before.motion) {
    return false;
  }
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (now.keys[axis] > before.keys[axis]) {
      return true;
    }
  }
  return false;
}
} // namespace phantomcrash
