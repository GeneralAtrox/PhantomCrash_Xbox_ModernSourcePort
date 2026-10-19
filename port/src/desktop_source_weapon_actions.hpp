#pragma once
#include "phantomcrash/battle.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/source_weapon_actions.hpp"
#include <map>
#include <memory>

namespace phantomcrash {
class BattleView;
class DesktopSourceWeaponActions final {
public:
  DesktopSourceWeaponActions(const DataRoots &roots, SourceParticles &particles,
                             const std::map<int, SourceParticleDefinition> &definitions,
                             const json::JsonValue &table, float hz, float world_scale);
  void observe(const Battle &battle, const BattleView &view, float seconds);

private:
  struct Player {
    int model{-1};
    std::unique_ptr<SourceWeaponActionPlayer> actions;
  };
  struct Effect {
    int owner{}, slot{};
    SourceActionEffect definition;
    bool held{}, stopped{};
  };
  struct Movement {
    Vec3 previous, displacement;
    bool ready{};
    unsigned lost{};
  };
  [[nodiscard]] bool alive(std::uint64_t key) const;
  [[nodiscard]] std::optional<SourceParticleParent>
  parent(const Battle &battle, const BattleView &view, const Effect &effect) const;
  void update_parents(const Battle &battle, const BattleView &view);
  void retire_owner(int owner);
  SourceParticles &particles_;
  const std::map<int, SourceParticleDefinition> &definitions_;
  std::map<std::pair<int, bool>, std::array<std::vector<SourceWeaponAction>, 4>> banks_;
  std::map<std::pair<int, int>, Player> players_;
  std::map<std::uint64_t, Effect> effects_;
  std::map<int, unsigned> pending_;
  std::map<int, unsigned> pending_loss_;
  std::map<int, Movement> movements_;
  float hz_{}, world_scale_{}, source_basis_scale_{1}, clock_{}, log_clock_{};
  std::uint64_t next_key_{0x51A3'0000'0000}, started_{}, loss_started_{}, missing_frames_{};
};
} // namespace phantomcrash
