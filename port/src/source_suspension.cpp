#include "phantomcrash/source_suspension.hpp"
#include "phantomcrash/json_fields.hpp"
#include <cmath>
#include <stdexcept>

namespace phantomcrash {

std::optional<SourceSuspension> parse_source_suspension(const json::JsonValue &node) {
  const auto *value = node.find("source_suspension");
  if (value == nullptr) {
    return std::nullopt;
  }
  SourceSuspension out;
  const auto read = [&](const char *field) {
    return static_cast<float>(json::required(*value, field).number());
  };
  out.period = read("period");
  out.split = read("split");
  out.low = read("low");
  out.span = read("span");
  out.high = read("high");
  out.phase = read("phase");
  out.frame_rate = read("frame_rate");
  if (!(out.period > out.split && out.split > 0 && out.phase >= 0 && out.phase < out.period &&
        out.frame_rate > 0) ||
      !std::isfinite(out.low) || !std::isfinite(out.span) || !std::isfinite(out.high) ||
      !std::isfinite(out.period) || !std::isfinite(out.frame_rate)) {
    throw std::runtime_error("Invalid source suspension cycle");
  }
  const auto *children = node.find("children");
  if (children && !children->array().empty()) {
    throw std::runtime_error("Source suspension requires a leaf control");
  }
  return out;
}

void apply_source_suspension(Mat4 &world, const std::optional<SourceSuspension> &cycle,
                             const double seconds) {
  if (!cycle || seconds <= 0) {
    return;
  }
  // 0x4e5da0 increments before sampling and writes world Y at node +0x84.
  // Pose reads never advance it. Native fixed ticks stand in for callback cadence.
  const auto &c = *cycle;
  const double ticks = std::floor(seconds * c.frame_rate + 1e-6);
  if (ticks < 1) {
    return;
  }
  const float phase = static_cast<float>(std::fmod(c.phase + ticks, c.period));
  const float offset = phase <= c.split ? c.low + c.span * phase / c.split
                                        : c.high - c.span * (phase - c.split) / c.split;
  world.at(3, 1) += offset;
}

} // namespace phantomcrash
