#pragma once

#include "phantomcrash/source_pmt.hpp"

namespace phantomcrash {
struct RigidClip;
struct RigidRig;
struct RigidNode;

// Owns each selected source control's key index and elapsed time. Evaluation
// and advancement are separate so every drawing/socket consumer sees one pose.
class RigidSourcePlayback {
public:
  void start(const RigidClip &clip, float frame = 0, bool loop = false);
  [[nodiscard]] bool active() const {
    return clip_ != nullptr;
  }
  [[nodiscard]] std::vector<RigidNode> sample(const RigidRig &rig) const;
  void advance(float frames, bool loop);

private:
  const RigidClip *clip_{};
  std::vector<SourcePmtCursor> cursors_;
};
} // namespace phantomcrash
