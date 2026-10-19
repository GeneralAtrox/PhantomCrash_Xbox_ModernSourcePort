#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"
#include <optional>

namespace phantomcrash {

struct SourceSuspension {
  float period{};
  float split{};
  float low{};
  float span{};
  float high{};
  float phase{};
  float frame_rate{};
};

[[nodiscard]] std::optional<SourceSuspension> parse_source_suspension(const json::JsonValue &node);
void apply_source_suspension(Mat4 &world, const std::optional<SourceSuspension> &cycle,
                             double seconds);

} // namespace phantomcrash
