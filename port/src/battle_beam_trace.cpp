#include "phantomcrash/battle_beam_trace.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>

namespace phantomcrash {
namespace {

constexpr float degrees_per_radian = 180.0F / pi;

std::FILE *trace_file = nullptr;
bool trace_opened = false;

std::FILE *open_trace() {
  if (!trace_opened) {
    trace_opened = true;
    const char *path = std::getenv("PHANTOMCRASH_BEAM_TRACE");
    if (path != nullptr && *path != '\0') {
      trace_file = std::fopen(path, "w");
    }
  }
  return trace_file;
}

} // namespace

bool beam_trace_enabled() {
  return open_trace() != nullptr;
}

void beam_trace_line(const std::string_view line) {
  if (std::FILE *file = open_trace()) {
    std::fwrite(line.data(), 1, line.size(), file);
    std::fputc('\n', file);
    std::fflush(file);
  }
}

void trace_beam_step(const Beam &beam, const Vec3 from, const Vec3 tip,
                     const std::vector<Target> &targets, const int victim,
                     const std::optional<Vec3> contact) {
  std::string text =
      std::format("step held={:.3f} length={:.2f} range={:.1f} origin=({:.2f},{:.2f},{:.2f}) "
                  "direction=({:.3f},{:.3f},{:.3f}) tip=({:.2f},{:.2f},{:.2f})",
                  beam.held, beam.length, beam.weapon->range, from.x, from.y, from.z,
                  beam.direction.x, beam.direction.y, beam.direction.z, tip.x, tip.y, tip.z);
  // The living enemy nearest the beam's line, and how far off it is aimed.
  int nearest = -1;
  float off_aim = 180.0F;
  for (std::size_t index = 0; index < targets.size(); ++index) {
    const Target &body = targets[index];
    const Vec3 to = body.centre - from;
    const float distance = length(to);
    if (!body.alive || body.team == beam.team || distance <= 0.0F) {
      continue;
    }
    const float cosine = std::clamp(dot(beam.direction, to * (1.0F / distance)), -1.0F, 1.0F);
    const float angle = std::acos(cosine) * degrees_per_radian;
    if (nearest < 0 || angle < off_aim) {
      nearest = static_cast<int>(index);
      off_aim = angle;
    }
  }
  if (nearest >= 0) {
    const Target &body = targets[static_cast<std::size_t>(nearest)];
    text += std::format(" nearest={} off_aim={:.2f}deg distance={:.2f} radius={:.2f} "
                        "centre=({:.2f},{:.2f},{:.2f})",
                        nearest, off_aim, length(body.centre - from), body.radius, body.centre.x,
                        body.centre.y, body.centre.z);
  }
  if (victim >= 0 && contact) {
    text += std::format(" HIT={} at=({:.2f},{:.2f},{:.2f})", victim, contact->x, contact->y,
                        contact->z);
  } else {
    text += " miss";
  }
  beam_trace_line(text);
}

} // namespace phantomcrash
