#pragma once

#include "desktop_world_renderer.hpp"
#include "mech_vfx_data.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/data_roots.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace phantomcrash {

// MechAssault's weapon effects (mech_vfx_data.hpp) drawn over the battle's
// own: a muzzle effect where a round leaves, a shot effect riding each
// projectile and an impact effect where one lands. Off unless
// mods/data/world/mechassault_vfx.json (from `convert-mechassault-vfx`) is
// there and sets "enabled".
class DesktopMechVfx final {
public:
  DesktopMechVfx(WorldRenderer &world, const DataRoots &roots);

  [[nodiscard]] bool enabled() const noexcept {
    return data_.enabled;
  }
  void tick(const Battle &battle, float seconds);
  // Appends the effects' meshes; facing nodes turn to the camera of `view`.
  void add_items(const Mat4 &view, std::vector<WorldItem> &items) const;

private:
  struct Instance {
    const VfxEffect *effect{};
    Mat4 frame; // the effect's heading and position, before the unit scale
    float age{};
    float life{-1.0F}; // seconds; below 0 it follows its projectile
    std::uint64_t projectile{};
    bool seen{};
  };
  [[nodiscard]] const VfxEffect *effect(const std::string &key) const;
  void start(const std::string &key, const Mat4 &frame, float default_life);
  void place_shots(const Battle &battle);
  void append(const Instance &instance, const Mat4 &view, std::vector<WorldItem> &items) const;

  WorldRenderer *world_;
  VfxData data_;
  std::map<std::string, int> models_;
  std::vector<Instance> instances_;
};

} // namespace phantomcrash
