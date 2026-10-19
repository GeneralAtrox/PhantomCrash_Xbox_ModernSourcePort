#include "desktop_point_lights.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

constexpr std::array<std::pair<const char *, int>, 6> hud_kinds{
    {{"cannon", hud_kind::cannon},
     {"missile", hud_kind::missile},
     {"machine_gun", hud_kind::machine_gun},
     {"grenade", hud_kind::grenade},
     {"beam", hud_kind::beam},
     {"melee", hud_kind::melee}}};
constexpr float shadow_inner_share = 0.7F; // a shadowed light's full-strength core
constexpr float shadow_near = 0.2F;

float number(const json::JsonValue &value, const char *key, const float fallback) {
  const float out = static_cast<float>(value.number_or(key, fallback));
  if (!std::isfinite(out) || out < 0.0F) {
    throw std::runtime_error(std::string("point_lights: invalid ") + key);
  }
  return out;
}

Vec3 colour(const json::JsonValue &value, const char *key, const Vec3 fallback) {
  const json::JsonValue *found = value.find(key);
  if (found == nullptr) {
    return fallback;
  }
  const auto c = json::numbers<3>(*found, key);
  return {c[0], c[1], c[2]};
}

PointLightLook look(const json::JsonValue *value, PointLightLook out = {}) {
  if (value == nullptr) {
    return out;
  }
  out.colour = colour(*value, "colour", out.colour);
  out.intensity = number(*value, "intensity", out.intensity);
  out.range = std::max(0.5F, number(*value, "range", out.range));
  out.seconds = std::max(0.001F, number(*value, "seconds", out.seconds));
  return out;
}

void by_kind(const json::JsonValue &value, const char *key, std::array<PointLightLook, 7> &out) {
  const json::JsonValue *group = value.find(key);
  if (group == nullptr) {
    return;
  }
  for (const auto &[name, kind] : hud_kinds) {
    out[static_cast<std::size_t>(kind)] = look(group->find(name));
  }
}

float importance(const WorldLight &light, const Vec3 eye) {
  const Vec3 to = light.position - eye;
  const float r2 = light.range * light.range;
  return light.intensity * r2 / (r2 + dot(to, to));
}

} // namespace

PointLightTuning load_point_light_tuning(const DataRoots &roots) {
  PointLightTuning t;
  const auto data = roots.document("world/dynamic_lighting.json");
  const json::JsonValue *value = data.find("point_lights");
  if (value == nullptr) {
    return t;
  }
  const json::JsonValue &v = *value;
  t.enabled = v.boolean_or("enabled", t.enabled);
  t.max_lights = std::clamp(static_cast<int>(number(v, "max_lights", 24.0F)), 0,
                            static_cast<int>(DynamicLightPass::point_capacity));
  t.day_scale = number(v, "day_scale", t.day_scale);
  t.air = std::max(0.0F, number(v, "air", t.air));
  t.shadowed = static_cast<int>(number(v, "shadowed", 0.0F));
  t.shadow_min = number(v, "shadow_min", t.shadow_min);
  t.shadow_lift = number(v, "shadow_lift", t.shadow_lift);
  t.shadow_degrees = std::clamp(number(v, "shadow_degrees", t.shadow_degrees), 10.0F, 85.0F);
  if (const json::JsonValue *effects = v.find("effects")) {
    const json::JsonValue &e = *effects;
    t.min_energy = number(e, "min_energy", t.min_energy);
    t.intensity_per_root = number(e, "intensity_per_root", t.intensity_per_root);
    t.max_intensity = number(e, "max_intensity", t.max_intensity);
    t.range_base = number(e, "range_base", t.range_base);
    t.range_per_root = number(e, "range_per_root", t.range_per_root);
    t.max_range = std::max(0.5F, number(e, "max_range", t.max_range));
    t.lift = number(e, "lift", t.lift);
    t.glow.flash_weight = number(e, "flash_weight", t.glow.flash_weight);
    t.glow.flash_colour = colour(e, "flash_colour", t.glow.flash_colour);
    if (const json::JsonValue *groups = e.find("group_scales")) {
      for (const auto &[key, scale] : groups->object()) {
        t.group_scales.emplace_back(std::stoi(key), static_cast<float>(scale.number()));
      }
    }
  }
  t.fireball = look(v.find("fireball"), t.fireball);
  if (const json::JsonValue *fireball = v.find("fireball")) {
    t.range_per_radius = number(*fireball, "range_per_radius", t.range_per_radius);
  }
  by_kind(v, "muzzle", t.muzzle);
  by_kind(v, "projectile", t.projectile);
  t.beam = look(v.find("beam"), t.beam);
  if (const json::JsonValue *beam = v.find("beam")) {
    t.beam_spacing = std::max(1.0F, number(*beam, "spacing", t.beam_spacing));
    t.beam_points = static_cast<int>(number(*beam, "points", 4.0F));
  }
  return t;
}

PointLights::PointLights(PointLightTuning tuning, const bool night)
    : tuning_(std::move(tuning)), scale_(night ? 1.0F : tuning_.day_scale) {
  const auto scales = tuning_.group_scales;
  tuning_.glow.group_scale = [scales](const int group) {
    const auto found = std::ranges::find(scales, group, &std::pair<int, float>::first);
    return found != scales.end() ? found->second : 1.0F;
  };
}

WorldLight PointLights::point(const Vec3 at, const PointLightLook &look,
                              const float strength) const {
  WorldLight light;
  light.point = true;
  light.position = at;
  light.colour = look.colour;
  light.intensity = look.intensity * strength * scale_;
  light.range = look.range;
  return light;
}

void PointLights::tick(const Battle &battle, const float seconds) {
  for (Flash &flash : flashes_) {
    flash.age += seconds;
  }
  std::erase_if(flashes_, [](const Flash &flash) { return flash.age >= flash.life; });
  for (const BattleEvent &event : battle.events()) {
    if (event.kind != BattleEventKind::fired || event.weapon == nullptr || event.slot < 0 ||
        event.slot >= weapon_slots || event.robot < 0 ||
        event.robot >= static_cast<int>(battle.robots().size())) {
      continue;
    }
    const auto kind = static_cast<std::size_t>(std::clamp(event.weapon->hud_kind, 0, 6));
    const PointLightLook &look = tuning_.muzzle[kind];
    if (look.intensity <= 0.0F) {
      continue;
    }
    const Combatant &robot = battle.robots()[static_cast<std::size_t>(event.robot)];
    const Vec3 at = battle.muzzle(robot, static_cast<WeaponSlot>(event.slot));
    flashes_.push_back({point(at, look, 1.0F), 0.0F, look.seconds});
  }
}

void PointLights::add_effects(const EffectSystem &effects, std::vector<Candidate> &out) const {
  for (const EffectGlow &glow : effect_glows(effects, tuning_.glow)) {
    if (glow.energy < tuning_.min_energy) {
      continue;
    }
    const float root = std::sqrt(glow.energy);
    WorldLight light;
    light.point = true;
    light.position = glow.position + Vec3{0.0F, tuning_.lift, 0.0F};
    light.colour = glow.colour;
    light.intensity = std::min(tuning_.max_intensity, tuning_.intensity_per_root * root) * scale_;
    light.range = std::min(tuning_.max_range, tuning_.range_base + tuning_.range_per_root * root);
    out.push_back({light, true});
  }
}

void PointLights::add_weapons(const Battle &battle, std::vector<Candidate> &out) const {
  const Armory &armory = battle.armory();
  if (tuning_.fireball.intensity > 0.0F) {
    for (const Explosion &blast : armory.explosions()) {
      const float left =
          blast.life > 0.0F ? std::clamp(1.0F - blast.age / blast.life, 0.0F, 1.0F) : 0.0F;
      WorldLight light = point(blast.position, tuning_.fireball, left * left);
      light.range = std::max(tuning_.fireball.range, blast.radius * tuning_.range_per_radius);
      out.push_back({light, true});
    }
  }
  for (const Projectile &round : armory.projectiles()) {
    const auto kind = static_cast<std::size_t>(std::clamp(round.weapon->hud_kind, 0, 6));
    const PointLightLook &look = tuning_.projectile[kind];
    if (look.intensity > 0.0F) {
      out.push_back({point(round.position, look, 1.0F), false});
    }
  }
  if (tuning_.beam.intensity > 0.0F) {
    for (const Beam &beam : armory.beams()) {
      const float strength = std::clamp(beam.held / tuning_.beam.seconds, 0.0F, 1.0F);
      const int points =
          std::min(tuning_.beam_points, 1 + static_cast<int>(beam.reach / tuning_.beam_spacing));
      for (int i = 0; i < points && strength > 0.0F; ++i) {
        const float along =
            beam.reach * (static_cast<float>(i) + 0.5F) / static_cast<float>(points);
        out.push_back({point(beam.origin + beam.direction * along, tuning_.beam, strength), false});
      }
    }
  }
  for (const Flash &flash : flashes_) {
    WorldLight light = flash.light;
    light.intensity *= 1.0F - flash.age / flash.life;
    out.push_back({light, false});
  }
}

void PointLights::add(const Battle &battle, const EffectSystem *effects, const Vec3 eye,
                      std::vector<WorldLight> &lights) const {
  if (!tuning_.enabled) {
    return;
  }
  std::vector<Candidate> candidates;
  if (effects != nullptr) {
    add_effects(*effects, candidates);
  }
  add_weapons(battle, candidates);
  std::erase_if(candidates, [](const Candidate &c) { return c.light.intensity <= 0.0F; });
  // The strongest blasts and fires take the shadow maps the spotlights leave.
  const auto spots = static_cast<int>(
      std::ranges::count_if(lights, [](const WorldLight &light) { return !light.point; }));
  const int shadowed =
      std::min(tuning_.shadowed, static_cast<int>(DynamicLightPass::capacity) - spots);
  std::ranges::sort(candidates, [](const Candidate &a, const Candidate &b) {
    return a.light.intensity * a.light.range > b.light.intensity * b.light.range;
  });
  int promoted = 0;
  for (Candidate &c : candidates) {
    if (promoted >= shadowed || !c.can_shadow ||
        c.light.intensity * c.light.range < tuning_.shadow_min) {
      continue;
    }
    WorldLight spot = c.light;
    spot.point = false;
    spot.position = spot.position + Vec3{0.0F, tuning_.shadow_lift, 0.0F};
    spot.direction = {0.0F, -1.0F, 0.0F};
    spot.outer_degrees = tuning_.shadow_degrees;
    spot.inner_degrees = tuning_.shadow_degrees * shadow_inner_share;
    spot.range += tuning_.shadow_lift;
    spot.near_plane = shadow_near;
    spot.air = tuning_.air;
    lights.push_back(spot);
    c.light.intensity = 0.0F; // now drawn as the spot
    ++promoted;
  }
  std::erase_if(candidates, [](const Candidate &c) { return c.light.intensity <= 0.0F; });
  std::ranges::sort(candidates, [&](const Candidate &a, const Candidate &b) {
    return importance(a.light, eye) > importance(b.light, eye);
  });
  const std::size_t kept =
      std::min(candidates.size(), static_cast<std::size_t>(tuning_.max_lights));
  for (std::size_t i = 0; i < kept; ++i) {
    lights.push_back(candidates[i].light);
    lights.back().air = tuning_.air;
  }
}

} // namespace phantomcrash
