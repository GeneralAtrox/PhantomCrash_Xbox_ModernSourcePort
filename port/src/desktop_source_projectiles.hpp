#pragma once

#include "desktop_world_renderer.hpp"
#include "phantomcrash/battle_weapons.hpp"
#include "phantomcrash/json.hpp"

namespace phantomcrash {

// Source ballistic model bindings. Flight and aim timing remain native.
class DesktopSourceProjectiles final {
public:
  DesktopSourceProjectiles(WorldRenderer &world, const json::JsonValue &routes);
  [[nodiscard]] bool contains(const WeaponTuning &weapon) const;
  bool add(const Projectile &shot, std::vector<WorldItem> &items) const;

private:
  struct Rule {
    int weapon_model{}, draw_model{-1};
    bool shoulder{};
    float scale{};
    mutable bool reported{};
  };
  [[nodiscard]] const Rule *find(const WeaponTuning &weapon) const;
  std::vector<Rule> rules_;
};

} // namespace phantomcrash
