#include "desktop_source_projectiles.hpp"

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {
Mat4 projectile_frame(const Vec3 velocity) {
  // Source launch velocity points down -Z. The glTF loader restores native
  // (x, -y, -z) axes, so the model's native +Z follows the flight direction.
  const Vec3 z = length(velocity) > 0.0F ? normalize(velocity) : Vec3{0.0F, 0.0F, 1.0F};
  const Vec3 up = std::fabs(z.y) < .99F ? Vec3{0.0F, 1.0F, 0.0F} : Vec3{1.0F, 0.0F, 0.0F};
  const Vec3 x = normalize(cross(up, z));
  const Vec3 y = cross(z, x);
  Mat4 result;
  const Vec3 rows[]{x, y, z};
  for (int row = 0; row < 3; ++row) {
    result.at(row, 0) = rows[row].x;
    result.at(row, 1) = rows[row].y;
    result.at(row, 2) = rows[row].z;
  }
  return result;
}
} // namespace

DesktopSourceProjectiles::DesktopSourceProjectiles(WorldRenderer &world,
                                                   const json::JsonValue &routes) {
  for (const auto &route : routes.array()) {
    Rule rule;
    rule.weapon_model = static_cast<int>(route.number_or("model", -1));
    rule.shoulder = route.string_or("mount", "") == "shoulder";
    rule.scale = static_cast<float>(route.number_or("scale", 1));
    rule.draw_model = world.model(route.string_or("file", ""));
    if (rule.draw_model >= 0) {
      rules_.push_back(rule);
    }
  }
  SDL_Log("S.L.A.I. projectiles: %zu ballistic model routes", rules_.size());
}

const DesktopSourceProjectiles::Rule *
DesktopSourceProjectiles::find(const WeaponTuning &weapon) const {
  if (!weapon.part) {
    return nullptr;
  }
  const bool shoulder = weapon.part->table == WeaponTable::shoulder;
  const auto rule = std::ranges::find_if(rules_, [&](const Rule &r) {
    return r.weapon_model == weapon.model && r.shoulder == shoulder;
  });
  return rule == rules_.end() ? nullptr : &*rule;
}

bool DesktopSourceProjectiles::contains(const WeaponTuning &weapon) const {
  return find(weapon) != nullptr;
}

bool DesktopSourceProjectiles::add(const Projectile &shot, std::vector<WorldItem> &items) const {
  const Rule *rule = find(*shot.weapon);
  if (!rule) {
    return false;
  }
  if (!rule->reported) {
    SDL_Log("S.L.A.I. projectile playback: weapon_model=%d mount=%s draw_model=%d scale=%.6f",
            rule->weapon_model, rule->shoulder ? "shoulder" : "arm", rule->draw_model, rule->scale);
    rule->reported = true;
  }
  WorldItem item;
  item.model = rule->draw_model;
  item.world = scaling({rule->scale, rule->scale, rule->scale}) * projectile_frame(shot.velocity) *
               translation(shot.position);
  items.push_back(std::move(item));
  return true;
}
} // namespace phantomcrash
