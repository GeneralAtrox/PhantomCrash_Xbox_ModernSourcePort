#include "desktop_mech_vfx.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <exception>

namespace phantomcrash {
namespace {

constexpr float fire_life = 0.3F; // when an effect names no kill time
constexpr float hit_life = 0.6F;
constexpr float fade_seconds = 0.08F; // alpha ramp into a kill

const char *kind_name(const int kind) {
  switch (kind) {
  case hud_kind::missile:
    return "missile";
  case hud_kind::machine_gun:
    return "machine_gun";
  case hud_kind::grenade:
    return "grenade";
  case hud_kind::beam:
    return "beam";
  default:
    return kind == hud_kind::cannon ? "cannon" : "";
  }
}

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

// The camera's turn: a model's xy plane faces the eye.
Mat4 facing(const Mat4 &view) {
  Mat4 m;
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      m.at(row, column) = view.at(column, row);
    }
  }
  return m;
}

Mat4 turn(const Vec3 axis, const float angle) {
  const float size = length(axis);
  if (size < 1.0e-6F || angle == 0.0F) {
    return {};
  }
  const float s = std::sin(angle * 0.5F) / size;
  return rotation(axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5F));
}

float between(const float a, const float b, const float t) {
  return std::clamp(t, std::min(a, b), std::max(a, b));
}

Vec3 scale_at(const VfxMotion &motion, const float age) {
  const Vec3 low = motion.scale_min;
  const Vec3 grown = low + motion.scale_delta * age;
  return {between(low.x, motion.scale_max.x, grown.x), between(low.y, motion.scale_max.y, grown.y),
          between(low.z, motion.scale_max.z, grown.z)};
}

} // namespace

DesktopMechVfx::DesktopMechVfx(WorldRenderer &world, const DataRoots &roots) : world_(&world) {
  try {
    data_ = parse_vfx(roots.document("world/mechassault_vfx.json"));
  } catch (const std::exception &error) {
    SDL_Log("world/mechassault_vfx.json: %s; the MechAssault effects stay off", error.what());
    return;
  }
  if (!data_.enabled) {
    return;
  }
  for (const auto &[key, effect] : data_.effects) {
    for (const VfxNode &node : effect.nodes) {
      if (!node.model.empty() && !models_.contains(node.model)) {
        models_[node.model] = world.model(node.model);
      }
    }
  }
  SDL_Log("MechAssault effects: %zu effects, %zu models", data_.effects.size(), models_.size());
}

const VfxEffect *DesktopMechVfx::effect(const std::string &key) const {
  const auto found = data_.effects.find(key);
  return key.empty() || found == data_.effects.end() ? nullptr : &found->second;
}

void DesktopMechVfx::start(const std::string &key, const Mat4 &frame, const float default_life) {
  const VfxEffect *found = effect(key);
  if (found == nullptr) {
    return;
  }
  Instance instance;
  instance.effect = found;
  instance.frame = frame;
  instance.life = found->length >= 0.0F ? found->length : default_life;
  instances_.push_back(instance);
}

void DesktopMechVfx::place_shots(const Battle &battle) {
  for (const Projectile &shot : battle.armory().projectiles()) {
    const auto weapon = data_.weapons.find(kind_name(shot.weapon->hud_kind));
    if (weapon == data_.weapons.end()) {
      continue;
    }
    const VfxEffect *found = effect(weapon->second.shot);
    if (found == nullptr) {
      continue;
    }
    const Mat4 frame = heading_along(shot.velocity) * translation(shot.position);
    auto instance = std::find_if(instances_.begin(), instances_.end(),
                                 [&](const Instance &i) { return i.projectile == shot.id; });
    if (instance == instances_.end()) {
      instances_.push_back({found, frame, 0.0F, -1.0F, shot.id, true});
    } else {
      instance->frame = frame;
      instance->seen = true;
    }
  }
}

void DesktopMechVfx::tick(const Battle &battle, const float seconds) {
  if (!data_.enabled) {
    return;
  }
  for (Instance &instance : instances_) {
    instance.age += seconds;
    instance.seen = false;
  }
  for (const BattleEvent &event : battle.events()) {
    if (event.weapon == nullptr) {
      continue;
    }
    const auto weapon = data_.weapons.find(kind_name(event.weapon->hud_kind));
    if (weapon == data_.weapons.end()) {
      continue;
    }
    if (event.kind == BattleEventKind::fired && event.robot >= 0) {
      const Vec3 aim = battle.aim(battle.robots().at(static_cast<std::size_t>(event.robot)));
      start(weapon->second.fire, heading_along(aim) * translation(event.at), fire_life);
    } else if (event.kind == BattleEventKind::impact) {
      const Vec3 normal = event.surface ? event.surface->normal : Vec3{0.0F, 1.0F, 0.0F};
      start(weapon->second.hit, heading_along(normal) * translation(event.at), hit_life);
    }
  }
  place_shots(battle);
  std::erase_if(instances_, [](const Instance &i) {
    return i.projectile != 0 ? !i.seen : i.life >= 0.0F && i.age > i.life;
  });
}

void DesktopMechVfx::append(const Instance &instance, const Mat4 &view,
                            std::vector<WorldItem> &items) const {
  const std::vector<VfxNode> &nodes = instance.effect->nodes;
  const float unit = data_.unit_scale;
  const Mat4 root = scaling({unit, unit, unit}) * instance.frame;
  std::vector<Mat4> worlds(nodes.size());
  std::vector<bool> alive(nodes.size(), true);
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const VfxNode &node = nodes[index];
    const Mat4 &parent = node.parent < 0 ? root : worlds[static_cast<std::size_t>(node.parent)];
    const VfxMotion &motion = node.motion;
    alive[index] = (node.parent < 0 || alive[static_cast<std::size_t>(node.parent)]) &&
                   (motion.kill < 0.0F || instance.age < motion.kill);
    const Vec3 size = scale_at(motion, instance.age);
    const Vec3 scale = {node.scale.x * size.x, node.scale.y * size.y, node.scale.z * size.z};
    const Mat4 spin = turn({1.0F, 0.0F, 0.0F}, motion.spin.x * instance.age) *
                      turn({0.0F, 1.0F, 0.0F}, motion.spin.y * instance.age) *
                      turn({0.0F, 0.0F, 1.0F}, motion.spin.z * instance.age);
    if (node.kind == VfxNodeKind::facing) {
      const Mat4 placed = translation(node.position) * parent;
      const Vec3 origin = {placed.at(3, 0), placed.at(3, 1), placed.at(3, 2)};
      worlds[index] = scaling({scale.x * unit, scale.y * unit, scale.z * unit}) * spin *
                      facing(view) * translation(origin);
    } else {
      worlds[index] =
          scaling(scale) * spin * turn(node.axis, node.angle) * translation(node.position) * parent;
    }
  }
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const VfxNode &node = nodes[index];
    const auto model = models_.find(node.model);
    if (!alive[index] || model == models_.end() || model->second < 0) {
      continue;
    }
    float alpha = node.motion.alpha_rate < 0.0F
                      ? std::max(0.0F, 1.0F + node.motion.alpha_rate * instance.age)
                      : 1.0F;
    if (node.motion.kill >= 0.0F) {
      alpha *= std::clamp((node.motion.kill - instance.age) / fade_seconds, 0.0F, 1.0F);
    }
    WorldItem item;
    item.model = model->second;
    item.world = worlds[index];
    item.alpha = alpha;
    items.push_back(std::move(item));
  }
}

void DesktopMechVfx::add_items(const Mat4 &view, std::vector<WorldItem> &items) const {
  for (const Instance &instance : instances_) {
    append(instance, view, items);
  }
}

} // namespace phantomcrash
