#include "phantomcrash/source_impact_attachment.hpp"
#include "phantomcrash/source_weapon_actions.hpp"
#include <stdexcept>

namespace phantomcrash {
SourceImpactAttachment source_impact_attachment(const Vec3 world_hit, const Mat4 &joint,
                                                const unsigned flags) {
  if (flags != 0x380c) {
    throw std::runtime_error("Unsupported model impact attachment mask");
  }
  const Mat4 frame = source_action_parent_frame(joint, flags, {}, false);
  const Vec3 relative = world_hit - transform_point({}, frame);
  // The mask reconstructs an orthonormal Euler basis, discarding model scale.
  const Vec3 local{dot(relative, {frame.at(0, 0), frame.at(0, 1), frame.at(0, 2)}),
                   dot(relative, {frame.at(1, 0), frame.at(1, 1), frame.at(1, 2)}),
                   dot(relative, {frame.at(2, 0), frame.at(2, 1), frame.at(2, 2)})};
  return {flags, local};
}

SourceParticleParent source_impact_parent(const SourceImpactAttachment &attachment,
                                          const Mat4 &joint) {
  SourceParticleParent parent;
  parent.flags = attachment.flags;
  parent.frame = source_action_parent_frame(joint, attachment.flags, attachment.local_hit, false);
  return parent;
}
} // namespace phantomcrash
