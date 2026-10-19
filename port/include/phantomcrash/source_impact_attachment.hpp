#pragma once
#include "phantomcrash/source_particles.hpp"

namespace phantomcrash {
struct SourceImpactAttachment {
  unsigned flags{};
  Vec3 local_hit;
};

// The native collision event gives a world point. Preserve that point when
// attaching, then transport it and the PTK axes with the masked model frame.
[[nodiscard]] SourceImpactAttachment source_impact_attachment(Vec3 world_hit, const Mat4 &joint,
                                                              unsigned flags);
[[nodiscard]] SourceParticleParent source_impact_parent(const SourceImpactAttachment &attachment,
                                                        const Mat4 &joint);
} // namespace phantomcrash
