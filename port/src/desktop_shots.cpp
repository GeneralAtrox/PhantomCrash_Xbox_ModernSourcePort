#include "desktop_shots.hpp"
#include "desktop_source_particles.hpp"

#include "desktop_cockpit.hpp"
#include "phantomcrash/skeleton.hpp"
#include "phantomcrash/json_fields.hpp"
#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <numbers>

namespace phantomcrash {
namespace {

constexpr float half = 0.5F; // grid_mdl and the sprites span -0.5 to 0.5
constexpr float byte_max = 255.0F;
constexpr float part_frame_rate = 60.0F; // the weapon tables count 60 Hz frames
constexpr int beam_ramp_step = 0x20;     // 0x09b9fe: frames held times 0x20202020

// +z along `direction`, for row vectors (the rows are the new axes).
Mat4 heading_along(const Vec3 direction) {
  const float size = length(direction);
  const Vec3 z = size > 0.0F ? direction * (1.0F / size) : Vec3{0.0F, 0.0F, 1.0F};
  const Vec3 helper = std::fabs(z.y) < 0.99F ? Vec3{0.0F, 1.0F, 0.0F} : Vec3{1.0F, 0.0F, 0.0F};
  const Vec3 x = normalize(cross(helper, z));
  const Vec3 y = cross(z, x);
  Mat4 m;
  const Vec3 rows[] = {x, y, z};
  for (int row = 0; row < 3; ++row) {
    m.at(row, 0) = rows[row].x;
    m.at(row, 1) = rows[row].y;
    m.at(row, 2) = rows[row].z;
  }
  return m;
}

// The camera's turn: a model's xy plane faces the eye (0x066223).
Mat4 facing(const Mat4 &view) {
  Mat4 m;
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      m.at(row, column) = view.at(column, row);
    }
  }
  return m;
}

// A beam's quad (0x09b9fe): x along the beam from `origin`, turned about
// it so the quad faces the camera's `toward` axis as far as it can.
Mat4 beam_frame(const Beam &beam, const Vec3 toward) {
  const Vec3 x = beam.direction;
  Vec3 z = toward - x * dot(toward, x);
  if (length(z) < 1.0e-4F) {
    z = std::fabs(x.y) < 0.99F ? Vec3{0.0F, 1.0F, 0.0F} : Vec3{1.0F, 0.0F, 0.0F};
    z = z - x * dot(z, x);
  }
  z = normalize(z);
  const Vec3 y = cross(z, x);
  Mat4 m;
  const Vec3 rows[] = {x, y, z};
  for (int row = 0; row < 3; ++row) {
    m.at(row, 0) = rows[row].x;
    m.at(row, 1) = rows[row].y;
    m.at(row, 2) = rows[row].z;
  }
  return m * translation(beam.origin);
}

Vec3 between(const Vec3 from, const Vec3 to, const float t) {
  return from + (to - from) * t;
}

} // namespace

FenceContactLook load_fence_contact(const DataRoots &roots) {
  try {
    return parse_fence_contact(roots.document("world/force_field.json"));
  } catch (const std::exception &error) {
    SDL_Log("world/force_field.json: %s; the touch ripples keep their defaults", error.what());
  }
  return {};
}

DesktopShots::DesktopShots(WorldRenderer &world, const EffectBank &bank, const DataRoots &roots,
                           const FenceContactLook contact, DesktopSourceParticles *source_particles)
    : bank_(&bank), residue_(world, bank), fence_(bank.fence, contact),
      source_particles_(source_particles) {
  for (const int mesh : bank.fence.meshes) {
    fence_meshes_.emplace(mesh, load_mesh(world, EffectPicture{bank.fence.model, mesh}));
  }
  for (const ShotLook &look : bank.shots) {
    looks_.push_back(load_mesh(world, look.model));
    look_sprites_.push_back(look.sprite.empty() ? -1 : world.texture(look.sprite));
  }
  exhaust_ = load_mesh(world, bank.exhaust);
  beam_ = load_mesh(world, bank.beam.model);
  try {
    const auto document = roots.document("world/muzzle_flashes.json");
    for (const auto &entry : json::required(document, "flashes").array()) {
      const int style = static_cast<int>(entry.number_or("style", -1));
      muzzle_looks_[style] = {
          load_mesh(world, EffectPicture{entry.string_or("model", ""),
                                         static_cast<int>(entry.number_or("mesh", -1))}),
          static_cast<float>(entry.number_or("scale", 3)),
          static_cast<float>(entry.number_or("seconds", 1.0 / 30.0))};
    }
  } catch (const std::exception &error) {
    SDL_Log("Muzzle flashes: %s", error.what());
  }
  for (const EffectFlash &flash : bank.flashes) {
    std::vector<int> &textures = flash_textures_.emplace_back();
    for (const std::string &frame : flash.frames) {
      textures.push_back(frame.empty() ? -1 : world.texture(frame));
    }
  }
}

DesktopShots::Mesh DesktopShots::load_mesh(WorldRenderer &world,
                                           const std::optional<EffectPicture> &picture) {
  Mesh out;
  if (!picture) {
    return out;
  }
  const int model = world.model(picture->model);
  const ModelData *data = world.model_data(model);
  if (data == nullptr) {
    return out;
  }
  const auto nodes = converter_mesh_nodes(*data);
  if (picture->mesh < 0 || picture->mesh >= static_cast<int>(nodes.size()) ||
      nodes[static_cast<std::size_t>(picture->mesh)].empty()) {
    return out;
  }
  out.model = model;
  out.nodes = nodes[static_cast<std::size_t>(picture->mesh)];
  return out;
}

void DesktopShots::add_mesh(const Mesh &mesh, const Mat4 &world, std::vector<WorldItem> &items) {
  for (const int node : mesh.nodes) {
    WorldItem item;
    item.model = mesh.model;
    item.node = node;
    item.world = world;
    items.push_back(std::move(item));
  }
}

void DesktopShots::tick(const Battle &battle, EffectSystem &effects, const float seconds,
                        const BattleView *view) {
  for (MuzzleFlash &flash : muzzle_flashes_) {
    flash.remaining -= seconds;
  }
  std::erase_if(muzzle_flashes_, [](const MuzzleFlash &flash) { return flash.remaining <= 0; });
  if (source_particles_) {
    source_particles_->observe(battle, view);
    if (view) {
      source_particles_->observe_actions(battle, *view, seconds);
    }
  }
  movement_.tick(battle, effects, seconds);
  mounts_.tick(battle, effects, seconds);
  fence_.tick(seconds);
  residue_.tick(battle, seconds);
  const EffectBank &bank = *bank_;
  for (const BattleEvent &event : battle.events()) {
    if (event.kind == BattleEventKind::fired && event.weapon &&
        !(source_particles_ && source_particles_->has_projectile(*event.weapon))) {
      const auto look = muzzle_looks_.find(event.weapon->fire_sound);
      if (look != muzzle_looks_.end() && look->second.mesh.model >= 0) {
        const float scale = look->second.scale;
        muzzle_flashes_.push_back({event.weapon->fire_sound,
                                   scaling({scale, scale, scale}) * heading_along(event.direction) *
                                       translation(event.at),
                                   look->second.seconds});
      }
    }
    if (event.object >= 0 && battle.objects()) {
      const auto &object = battle.objects()->objects().at(static_cast<std::size_t>(event.object));
      SDL_Log("Scenery destroyed: object=%d mesh=%d solid=%d collision triangles=%zu", event.object,
              object.visible_mesh(), object.solid(), object.collision.walls().size());
    }
    if (event.kind == BattleEventKind::fence_contact && event.surface) {
      ++fence_contact_requests_;
      if (fence_.contact(*event.surface, event.robot)) {
        ++fence_contact_starts_;
      }
    } else if (event.kind == BattleEventKind::impact && event.weapon != nullptr) {
      if (source_particles_ && source_particles_->impact(event, battle, view)) {
        continue;
      }
      if (event.surface && starts_impact(*event.weapon, event.on_robot)) {
        fence_.start(*event.surface);
      }
      // 0x098eda: where the weapon's landing case starts its impact.
      const int impact = event.weapon->impact;
      if (impact < 0 || impact >= static_cast<int>(bank.impacts.size()) ||
          !starts_impact(*event.weapon, event.on_robot)) {
        continue;
      }
      const ShotImpact &data = bank.impacts[static_cast<std::size_t>(impact)];
      residue_.impact(battle, event, data);
      effects.start(data.group, event.at, event.at, {0.0F, 1.0F, 0.0F}, data.flash_scale);
    } else if (event.kind == BattleEventKind::destroyed) {
      for (const int group : bank.wreck_groups) {
        effects.start(group, event.at, event.at);
      }
    } else if (event.kind == BattleEventKind::effect ||
               event.kind == BattleEventKind::wreck_destroyed) {
      effects.start(event.slot, event.at, event.at);
    }
  }
  constexpr float fence_log_interval = 2.0F;
  fence_log_seconds_ += seconds;
  if (fence_log_seconds_ >= fence_log_interval) {
    const bool active = fence_contact_requests_ != 0 || fence_.active_contacts() != 0;
    if (active || fence_log_active_) {
      SDL_Log("Force field: %.2fs, contact requests=%zu new=%zu live contacts=%zu "
              "total ripples=%zu/%d",
              fence_log_seconds_, fence_contact_requests_, fence_contact_starts_,
              fence_.active_contacts(), fence_.active(), bank.fence.capacity);
    }
    fence_log_active_ = active;
    fence_log_seconds_ = 0;
    fence_contact_requests_ = fence_contact_starts_ = 0;
  }
}

void DesktopShots::add_items(const Battle &battle, const Mat4 &view,
                             std::vector<WorldItem> &items) {
  for (const auto &frame : fence_.frames()) {
    const auto &mesh = fence_meshes_.at(frame.mesh);
    for (const int node : mesh.nodes) {
      WorldItem item;
      item.model = mesh.model;
      item.node = node;
      item.world = frame.world;
      item.optical = frame.refraction;
      // The original's ripples take no local light; skipping it keeps a
      // wake of overlapping, screen-wide ripples cheap to draw.
      item.local_light = false;
      items.push_back(std::move(item));
    }
  }
  const Mat4 camera = facing(view);
  std::uniform_real_distribution<float> roll(0.0F, 2.0F * std::numbers::pi_v<float>);
  add_beams(battle, camera, items);
  for (const MuzzleFlash &flash : muzzle_flashes_) {
    const std::size_t first = items.size();
    add_mesh(muzzle_looks_.at(flash.style).mesh, flash.world, items);
    for (std::size_t index = first; index < items.size(); ++index) {
      items[index].additive = true;
      items[index].local_light = false;
    }
  }
  for (const Projectile &shot : battle.armory().projectiles()) {
    if (source_particles_ && source_particles_->add_projectile(shot, items)) {
      continue;
    }
    const int look = shot.weapon->look;
    if (look < 0 || look >= static_cast<int>(looks_.size())) {
      continue;
    }
    const ShotLook &data = bank_->shots[static_cast<std::size_t>(look)];
    const Mesh &mesh = looks_[static_cast<std::size_t>(look)];
    if (mesh.model < 0) {
      continue;
    }
    // The six missile meshes (83-85, 91-93) point along -Z after the
    // glTF loader restores game coordinates. Their exhaust extends along +Z.
    const Vec3 model_heading = data.trail ? shot.velocity * -1.0F : shot.velocity;
    const Mat4 place =
        (data.billboard ? camera : heading_along(model_heading)) * translation(shot.position);
    add_mesh(mesh, scaling({data.scale, data.scale, data.scale}) * place, items);
    if (data.trail && exhaust_.model >= 0) {
      // 0x09831e: the flame behind, turned about the flight each frame.
      const Mat4 flame = scaling(data.exhaust_scale) * rotation_z(roll(random_)) *
                         translation({0.0F, 0.0F, data.exhaust_offset});
      add_mesh(exhaust_, flame * place, items);
    }
  }
}

// The held beams, stretched to what their tips strike. Over their fade
// they brighten frame by frame as held, and dim the same way let go.
void DesktopShots::add_beams(const Battle &battle, const Mat4 &camera,
                             std::vector<WorldItem> &items) {
  if (beam_.model < 0) {
    return;
  }
  const BeamLook &look = bank_->beam;
  const Vec3 toward{camera.at(2, 0), camera.at(2, 1), camera.at(2, 2)};
  for (const Beam &beam : battle.armory().beams()) {
    if (beam.reach <= 0.0F) {
      continue;
    }
    const Mat4 world = scaling({beam.reach, look.width, 1.0F}) *
                       translation({beam.reach * half, 0.0F, 0.0F}) * beam_frame(beam, toward);
    const std::size_t first = items.size();
    add_mesh(beam_, world, items);
    // 0x09b9fe sets the beam batch's flags 0x300 before drawing it: unlit
    // and added to what is behind (SRCALPHA, ONE), a glow rather than the
    // plain blend its model records (flags 0x102).
    for (std::size_t index = first; index < items.size(); ++index) {
      items[index].additive = true;
    }
    const float frames = beam.held * part_frame_rate;
    if (frames < look.fade_frames) {
      const int level = std::max(static_cast<int>(std::lround(frames)), 0) * beam_ramp_step;
      const float alpha = static_cast<float>(level) / byte_max;
      for (std::size_t index = first; index < items.size(); ++index) {
        items[index].alpha = alpha;
      }
    }
  }
}

void DesktopShots::add_sprites(const Battle &battle, const EffectSystem &effects,
                               std::vector<WorldSprite> &sprites) const {
  residue_.add_sprites(sprites);
  for (const Projectile &shot : battle.armory().projectiles()) {
    if (source_particles_ && source_particles_->has_projectile(*shot.weapon)) {
      continue;
    }
    const int look = shot.weapon->look;
    if (look < 0 || look >= static_cast<int>(look_sprites_.size()) ||
        look_sprites_[static_cast<std::size_t>(look)] < 0) {
      continue;
    }
    WorldSprite sprite;
    sprite.texture = look_sprites_[static_cast<std::size_t>(look)];
    sprite.position = shot.position;
    sprite.size = bank_->shots[static_cast<std::size_t>(look)].scale * half;
    sprite.additive = true;
    sprites.push_back(sprite);
  }
  const float t = effects.between();
  for (const EffectFlashState &flash : effects.flashes()) {
    const auto &textures = flash_textures_[static_cast<std::size_t>(flash.flash)];
    const int frame = std::clamp(flash.age, 0, static_cast<int>(textures.size()) - 1);
    if (textures.empty() || textures[static_cast<std::size_t>(frame)] < 0) {
      continue;
    }
    WorldSprite sprite;
    sprite.texture = textures[static_cast<std::size_t>(frame)];
    sprite.position = between(flash.previous, flash.position, t);
    sprite.size = flash.scale * half;
    sprite.colour = {1.0F, 1.0F, 1.0F, static_cast<float>(flash.alpha) / byte_max};
    sprite.additive = bank_->flashes[static_cast<std::size_t>(flash.flash)].additive;
    sprites.push_back(sprite);
  }
}

} // namespace phantomcrash
