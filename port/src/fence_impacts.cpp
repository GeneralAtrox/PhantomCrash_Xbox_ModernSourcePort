#include "phantomcrash/fence_impacts.hpp"
#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace phantomcrash {
namespace {

constexpr double track_forget_seconds = 0.25; // a longer gap starts a new slide
constexpr float velocity_ease = 0.5F;         // how much of each new slide speed is taken

// The hit's surface frame: y along the normal, x along `along` where it
// lies in the surface.
Mat4 surface_frame(const Vec3 at, const Vec3 normal, const Vec3 along = {}) {
  const Vec3 y = normalize(normal);
  const Vec3 flat = along - y * dot(along, y);
  const Vec3 helper = std::abs(y.y) < 0.99F ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
  const Vec3 x = length(flat) > 0.001F ? normalize(flat) : normalize(cross(helper, y));
  const Vec3 z = cross(x, y);
  Mat4 out = translation(at);
  const Vec3 axes[] = {x, y, z};
  for (int row = 0; row < 3; ++row) {
    out.at(row, 0) = axes[row].x;
    out.at(row, 1) = axes[row].y;
    out.at(row, 2) = axes[row].z;
  }
  return out;
}

} // namespace

FenceLook parse_fence_look(const json::JsonValue &value) {
  FenceLook out;
  out.model = value.string_or("model", "");
  if (const auto *meshes = value.find("meshes")) {
    for (const auto &mesh : meshes->array()) {
      out.meshes.push_back(static_cast<int>(mesh.number()));
    }
  }
  out.capacity = static_cast<int>(value.number_or("capacity", out.capacity));
  out.frame_rate = static_cast<float>(value.number_or("frame_rate", out.frame_rate));
  out.scale = static_cast<float>(value.number_or("scale", out.scale));
  out.growth = static_cast<float>(value.number_or("growth", out.growth));
  out.depth = static_cast<float>(value.number_or("depth", out.depth));
  out.offset = static_cast<float>(value.number_or("offset", out.offset));
  if (const auto *refraction = value.find("refraction")) {
    out.refraction.edge = static_cast<float>(refraction->number_or("edge", out.refraction.edge));
    out.refraction.reach = static_cast<float>(refraction->number_or("reach", out.refraction.reach));
    out.refraction.push = static_cast<float>(refraction->number_or("push", out.refraction.push));
    out.refraction.least = static_cast<float>(refraction->number_or("least", out.refraction.least));
  }
  if (out.capacity < 1 || out.frame_rate <= 0 || out.refraction.edge <= 0 ||
      out.refraction.reach <= 0) {
    throw std::runtime_error("Invalid fence impact timing or refraction");
  }
  return out;
}

FenceContactLook parse_fence_contact(const json::JsonValue &value) {
  FenceContactLook out;
  const auto number = [&](const char *key, float &field) {
    field = static_cast<float>(value.number_or(key, field));
  };
  number("spacing", out.spacing);
  number("refresh_seconds", out.refresh_seconds);
  out.trail = static_cast<int>(value.number_or("trail", out.trail));
  out.limit = static_cast<int>(value.number_or("limit", out.limit));
  number("frame_rate", out.frame_rate);
  number("growth", out.growth);
  number("follow_rate", out.follow_rate);
  number("stretch", out.stretch);
  number("stretch_limit", out.stretch_limit);
  number("fade", out.fade);
  if (out.spacing <= 0 || out.refresh_seconds <= 0 || out.trail < 1 || out.limit < 1 ||
      out.frame_rate <= 0 || out.follow_rate < 0 || out.stretch < 0 || out.stretch_limit < 0 ||
      out.fade < 0) {
    throw std::runtime_error("Invalid force-field contact spacing, timing or smear");
  }
  return out;
}

void FenceImpacts::start(const StageHit &hit) {
  const auto shots = std::count_if(impacts_.begin(), impacts_.end(),
                                   [](const Impact &impact) { return impact.robot < 0; });
  if ((surface_flags(hit.surface) & stage_contact::fence) == 0 || look_.meshes.empty() ||
      shots >= look_.capacity) {
    return;
  }
  impacts_.push_back({hit.position + hit.normal * look_.offset, hit.normal});
}

void FenceImpacts::tick(const float seconds) {
  clock_ += seconds;
  const float follow = 1.0F - std::exp(-contact_.follow_rate * seconds);
  for (auto &impact : impacts_) {
    impact.age +=
        static_cast<double>(seconds) * (impact.robot >= 0 ? contact_.frame_rate : look_.frame_rate);
    if (impact.following) {
      impact.at = impact.at + (impact.target - impact.at) * follow;
    }
  }
  std::erase_if(impacts_, [&](const Impact &impact) {
    return impact.age >= static_cast<double>(look_.meshes.size());
  });
  std::erase_if(tracks_, [&](const auto &entry) {
    return clock_ - entry.second.seen > track_forget_seconds;
  });
}

void FenceImpacts::smear(Impact &impact, const Track &track) const {
  const float speed = length(track.velocity);
  impact.along = track.velocity;
  impact.stretch = 1.0F + std::min(speed * contact_.stretch, contact_.stretch_limit);
}

bool FenceImpacts::contact(const StageHit &hit, const int robot) {
  if (robot < 0 || (surface_flags(hit.surface) & stage_contact::fence) == 0 ||
      look_.meshes.empty()) {
    return false;
  }
  const Vec3 at = hit.position + hit.normal * look_.offset;
  Track &track = tracks_[robot];
  const double since = clock_ - track.seen;
  if (track.seen >= 0.0 && since > 0.0 && since <= track_forget_seconds) {
    const Vec3 moved = (at - track.at) * static_cast<float>(1.0 / since);
    track.velocity = track.velocity + (moved - track.velocity) * velocity_ease;
  } else if (track.seen < 0.0 || since > track_forget_seconds) {
    track.velocity = {};
  }
  track.at = at;
  track.seen = clock_;

  Impact *head = nullptr;
  for (auto &impact : impacts_) {
    if (impact.robot == robot && impact.following) {
      head = &impact;
    }
  }
  if (head != nullptr && length(at - head->anchor) < contact_.spacing &&
      clock_ - head->born < contact_.refresh_seconds) {
    head->target = at;
    head->normal = hit.normal;
    smear(*head, track);
    return false;
  }
  // The newest is left behind in the wake; the robot's oldest makes room.
  if (head != nullptr) {
    head->following = false;
  }
  const auto mine = std::count_if(impacts_.begin(), impacts_.end(),
                                  [&](const Impact &impact) { return impact.robot == robot; });
  if (mine >= contact_.trail) {
    impacts_.erase(std::find_if(impacts_.begin(), impacts_.end(),
                                [&](const Impact &impact) { return impact.robot == robot; }));
  }
  // A crowd along the field shares one budget, so its cost stays bounded.
  if (active_contacts() >= static_cast<std::size_t>(contact_.limit)) {
    impacts_.erase(std::find_if(impacts_.begin(), impacts_.end(),
                                [](const Impact &impact) { return impact.robot >= 0; }));
  }
  Impact fresh{at, hit.normal, 0.0, robot, at, at, clock_, true};
  smear(fresh, track);
  impacts_.push_back(fresh);
  return true;
}

std::size_t FenceImpacts::active_contacts() const {
  return static_cast<std::size_t>(std::count_if(
      impacts_.begin(), impacts_.end(), [](const Impact &impact) { return impact.robot >= 0; }));
}

float FenceImpacts::progress(const Impact &impact) const {
  return static_cast<float>(impact.age / static_cast<double>(look_.meshes.size()));
}

std::vector<FenceFrame> FenceImpacts::frames() const {
  std::vector<FenceFrame> out;
  if (look_.meshes.empty()) {
    return out;
  }
  out.reserve(impacts_.size());
  const std::size_t last = look_.meshes.size() - 1;
  for (const auto &impact : impacts_) {
    const auto frame = std::min(static_cast<std::size_t>(impact.age), last);
    OpticalSphere refraction = look_.refraction;
    refraction.centre = impact.at;
    if (impact.robot < 0) {
      // 0x071869: a shot's ripple grows a step each 30 Hz frame.
      const float scale = look_.scale + look_.growth * static_cast<float>(frame) /
                                            static_cast<float>(look_.meshes.size());
      out.push_back({look_.meshes[frame],
                     scaling({scale, look_.depth, scale}) * surface_frame(impact.at, impact.normal),
                     refraction});
      continue;
    }
    // A touch spreads smoothly, smeared along the slide, and fades away.
    const float done = std::clamp(progress(impact), 0.0F, 1.0F);
    const float scale = look_.scale + contact_.growth * done;
    if (contact_.fade > 0.0F) {
      refraction.push *= std::pow(1.0F - done, contact_.fade);
    }
    out.push_back({look_.meshes[frame],
                   scaling({scale * impact.stretch, look_.depth, scale}) *
                       surface_frame(impact.at, impact.normal, impact.along),
                   refraction});
  }
  return out;
}

} // namespace phantomcrash
