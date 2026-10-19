#include "desktop_shot_residue.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {

constexpr int fit_probes = 8;         // points round the mark tested for a surface
constexpr float fit_depth = 0.25F;    // probe reach either side, of the half width
constexpr float fit_tolerance = 0.1F; // off the plane still on it, of the half width
constexpr int fit_tries = 2;          // the size, then half of it
constexpr int scorch_kind = 1;

// The largest of `half` and its halves whose ring of probes all find a
// surface on the hit's plane; 0 when none does.
float fitted_half(const StageCollision &stage, const StageHit &hit, const Vec3 normal, float half,
                  const float reach) {
  const Vec3 helper = std::fabs(normal.y) < 0.99F ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
  const Vec3 across = normalize(cross(helper, normal));
  const Vec3 along = cross(normal, across);
  for (int attempt = 0; attempt < fit_tries; ++attempt, half *= 0.5F) {
    bool fits = true;
    for (int i = 0; i < fit_probes && fits; ++i) {
      const float angle = 2.0F * pi * static_cast<float>(i) / fit_probes;
      const Vec3 point =
          hit.position + (across * std::cos(angle) + along * std::sin(angle)) * (half * reach);
      const auto found = stage.segment_contact(point + normal * (half * fit_depth),
                                               point - normal * (half * fit_depth));
      fits =
          found && std::fabs(dot(found->position - hit.position, normal)) <= half * fit_tolerance;
    }
    if (fits) {
      return half;
    }
  }
  return 0.0F;
}

} // namespace

DesktopShotResidue::DesktopShotResidue(WorldRenderer &world, const EffectBank &bank) : bank_(bank) {
  for (const auto &path : bank.decals.textures) {
    textures_.push_back(path.empty() ? -1 : world.texture(path));
  }
  if (!bank.missile_trail.texture.empty()) {
    smoke_texture_ = world.texture(bank.missile_trail.texture);
  }
  const auto loaded = std::count_if(textures_.begin(), textures_.end(),
                                    [](const int texture) { return texture >= 0; });
  SDL_Log("Weapon residue: %d/%d decal textures, smoke texture %s, sampling %.1f Hz",
          static_cast<int>(loaded), static_cast<int>(textures_.size()),
          smoke_texture_ >= 0 ? "loaded" : "unavailable", bank.missile_trail.sample_hz);
}

void DesktopShotResidue::mark(const StageCollision &stage, const StageHit &hit, const int kind,
                              const float size) {
  if (bank_.decals.count <= 0 || kind >= static_cast<int>(textures_.size()) ||
      textures_[kind] < 0 || hit.surface % 1000 == 8 || length(hit.normal) <= 0.0F) {
    return;
  }
  WorldSprite mark;
  mark.texture = textures_[kind];
  mark.surface_normal = normalize(hit.normal);
  mark.position = hit.position + mark.surface_normal * bank_.decals.depth_offset;
  mark.size = fitted_half(stage, hit, mark.surface_normal, size * 0.5F, bank_.decals.fit_reach);
  if (mark.size <= 0.0F) {
    return;
  }
  if (kind == scorch_kind) {
    const auto &tint = bank_.decals.scorch_tint;
    mark.colour = {tint[0], tint[1], tint[2], 1.0F};
  }
  mark.rotation = mark_rotation_;
  mark_rotation_ = std::fmod(mark_rotation_ + 1.0F, 2.0F * pi); // 0x0663d2
  marks_.push_back(mark);
  while (marks_.size() > static_cast<std::size_t>(bank_.decals.count)) {
    marks_.pop_front();
  }
}

void DesktopShotResidue::impact(const Battle &battle, const BattleEvent &event,
                                const ShotImpact &impact) {
  if (!event.weapon || event.on_robot) {
    return;
  }
  const float radius = event.weapon->blast_radius;
  if (radius > 0.0F) {
    // The game contains a second scorch texture in its decal ring. Place
    // it on the struck solid surface, or the ground reached by an airburst.
    auto surface = event.surface;
    if (!surface) {
      surface = battle.stage().segment_contact(event.at, event.at - Vec3{0, radius, 0});
    }
    if (surface) {
      mark(battle.stage(), *surface, scorch_kind, radius * bank_.decals.scorch_scale);
    }
  } else if (impact.decal && event.surface) {
    mark(battle.stage(), *event.surface, 0, bank_.decals.scale);
  }
}

void DesktopShotResidue::tick(const Battle &battle, const float seconds) {
  const auto &look = bank_.missile_trail;
  for (auto &[id, trail] : trails_) {
    trail.seen = false;
  }
  const float period = 1.0F / look.sample_hz;
  for (const Projectile &shot : battle.armory().projectiles()) {
    const int index = shot.weapon->look;
    if (smoke_texture_ < 0 || index < 0 || index >= static_cast<int>(bank_.shots.size()) ||
        !bank_.shots[index].trail) {
      continue;
    }
    auto found = trails_.find(shot.id);
    if (found == trails_.end()) {
      if (trails_.size() >= static_cast<std::size_t>(look.capacity)) {
        continue;
      }
      found = trails_.emplace(shot.id, Trail{}).first;
      found->second.previous = shot.position - shot.velocity * std::min(seconds, shot.age);
      found->second.points.push_front(found->second.previous);
    }
    Trail &trail = found->second;
    trail.seen = true;
    trail.stopped = 0.0F;
    trail.clock += seconds;
    while (trail.clock >= period) {
      trail.clock -= period;
      const float share = seconds > 0 ? std::clamp(1.0F - trail.clock / seconds, 0.0F, 1.0F) : 1;
      trail.points.push_front(trail.previous + (shot.position - trail.previous) * share);
      if (trail.points.size() > static_cast<std::size_t>(look.points)) {
        trail.points.pop_back();
      }
    }
    trail.previous = shot.position;
  }
  for (auto &[id, trail] : trails_) {
    if (!trail.seen) {
      trail.stopped += seconds;
    }
  }
  std::erase_if(trails_,
                [&](const auto &entry) { return entry.second.stopped >= look.fade_seconds; });
}

void DesktopShotResidue::add_sprites(std::vector<WorldSprite> &sprites) const {
  sprites.insert(sprites.end(), marks_.begin(), marks_.end());
  const auto &look = bank_.missile_trail;
  for (const auto &[id, trail] : trails_) {
    const float count = static_cast<float>(trail.points.size());
    const float fade = std::clamp(1.0F - trail.stopped / look.fade_seconds, 0.0F, 1.0F);
    for (std::size_t i = 1; i < trail.points.size(); ++i) {
      WorldSprite sprite;
      sprite.texture = smoke_texture_;
      sprite.position = trail.points[i - 1];
      sprite.end = trail.points[i];
      sprite.streak = true;
      // 1583/0 fades at V=0/1, while U carries the repeating smoke detail.
      // Put V across the ribbon; the particle streak convention puts it along
      // the flight, producing opaque rectangular sides and gaps at each sample.
      sprite.transpose_uv = true;
      const float near_share = static_cast<float>(i - 1) / count;
      const float far_share = static_cast<float>(i) / count;
      const float tail = count * look.tail_width / static_cast<float>(look.points);
      sprite.size = std::lerp(look.head_width, tail, near_share);
      sprite.end_size =
          i + 1 == trail.points.size() ? 0.0F : std::lerp(look.head_width, tail, far_share);
      sprite.colour[3] = (1.0F - near_share) * fade;
      sprite.end_alpha = (1.0F - far_share) / (1.0F - near_share);
      sprites.push_back(sprite);
    }
  }
}

} // namespace phantomcrash
