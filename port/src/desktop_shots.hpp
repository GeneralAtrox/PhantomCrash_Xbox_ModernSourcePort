#pragma once

#include "desktop_world_renderer.hpp"
#include "desktop_shot_residue.hpp"
#include "desktop_mount_effects.hpp"
#include "desktop_movement_effects.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/effect_system.hpp"

#include <optional>
#include <random>
#include <vector>

namespace phantomcrash {
class DesktopSourceParticles;
class BattleView;

// The battle's shots and what they set off, from the effect bank's tables
// (effect_bank.hpp): each shot drawn with its look's model (0x09831e), a
// trailing missile's exhaust behind it; the impact group a landing shot
// starts (0x098eda, 0x065f28) and the groups a wreck starts (0x07675e);
// and the group flashes, drawn as sprites facing the camera (0x065ade).
// data/world/force_field.json (FenceContactLook), or its defaults when it is
// missing or wrong.
[[nodiscard]] FenceContactLook load_fence_contact(const DataRoots &roots);

class DesktopShots final {
public:
  // `bank` must outlive this (DesktopEffects' system keeps it); `contact`
  // is how a robot's touch spreads over the force field.
  DesktopShots(WorldRenderer &world, const EffectBank &bank, const DataRoots &roots,
               FenceContactLook contact = {}, DesktopSourceParticles *source_particles = nullptr);

  // Starts the effects of this tick's landings, wrecks and blasts.
  void tick(const Battle &battle, EffectSystem &effects, float seconds,
            const BattleView *view = nullptr);
  // Appends the shots' models; billboard looks face the camera of `view`.
  void add_items(const Battle &battle, const Mat4 &view, std::vector<WorldItem> &items);
  // Appends the sprite shots and the flashes in play.
  void add_sprites(const Battle &battle, const EffectSystem &effects,
                   std::vector<WorldSprite> &sprites) const;

private:
  struct Mesh {
    int model{-1};
    std::vector<int> nodes;
  };
  [[nodiscard]] Mesh load_mesh(WorldRenderer &world, const std::optional<EffectPicture> &picture);
  void add_beams(const Battle &battle, const Mat4 &camera, std::vector<WorldItem> &items);
  static void add_mesh(const Mesh &mesh, const Mat4 &world, std::vector<WorldItem> &items);

  const EffectBank *bank_;
  DesktopShotResidue residue_;
  DesktopMovementEffects movement_;
  DesktopMountEffects mounts_;
  std::vector<Mesh> looks_;
  std::vector<int> look_sprites_;
  Mesh exhaust_;
  Mesh beam_;
  struct MuzzleLook {
    Mesh mesh;
    float scale{3.0F};
    float seconds{1.0F / 30.0F};
  };
  struct MuzzleFlash {
    int style{};
    Mat4 world;
    float remaining{};
  };
  std::map<int, MuzzleLook> muzzle_looks_;
  std::vector<MuzzleFlash> muzzle_flashes_;
  FenceImpacts fence_;
  float fence_log_seconds_{};
  std::size_t fence_contact_requests_{};
  std::size_t fence_contact_starts_{};
  bool fence_log_active_{};
  std::map<int, Mesh> fence_meshes_;
  std::vector<std::vector<int>> flash_textures_;
  std::minstd_rand random_{0x5eed};
  DesktopSourceParticles *source_particles_{};
};

} // namespace phantomcrash
