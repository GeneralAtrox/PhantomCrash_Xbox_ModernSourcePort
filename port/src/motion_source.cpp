#include "phantomcrash/skeleton.hpp"

#include <algorithm>

namespace phantomcrash {
namespace {

RootSplit split(const Vec3 at, const bool keep_sway) {
  return {{keep_sway ? at.x : 0.0F, at.y, 0.0F}, {keep_sway ? 0.0F : -at.x, 0.0F, -at.z}};
}

} // namespace

TrackSample MotionSource::sample(const int track) const {
  if (motion == nullptr) {
    return {};
  }
  const TrackSample current = motion->sample(track, frame);
  if (!blending()) {
    return current;
  }
  const auto as_sample = [&](const MotionLayer &layer) {
    return layer.motion->sample(track, layer.frame);
  };
  const auto mix = [](const TrackSample &a, const TrackSample &b, const float share) {
    return blend(a, b, share);
  };
  const auto from = blend_from<TrackSample>(as_sample, mix);
  return from ? blend(*from, current, std::max(weight, 0.0F)) : current;
}

RootSplit MotionSource::root_split(const int track, const Vec3 bind, const Vec3 sampled,
                                   const bool keep_sway) const {
  if (motion == nullptr || !blending()) {
    return split(sampled, keep_sway);
  }
  // The skeleton's axes: the evaluator flips Y of an animated translation.
  const auto as_split = [&](const MotionLayer &layer) {
    const auto at = layer.motion->sample(track, layer.frame).translation;
    return split(at ? Vec3{at->x, -at->y, at->z} : bind, layer.keep_sway);
  };
  const auto mix = [](const RootSplit &a, const RootSplit &b, const float share) {
    return RootSplit{a.body + (b.body - a.body) * share, a.sway + (b.sway - a.sway) * share};
  };
  const RootSplit current = as_split({motion, frame, 1.0F, keep_sway});
  const auto from = blend_from<RootSplit>(as_split, mix);
  return from ? mix(*from, current, std::max(weight, 0.0F)) : current;
}

} // namespace phantomcrash
