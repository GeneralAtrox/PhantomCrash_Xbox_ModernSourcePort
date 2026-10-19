#pragma once
#include "phantomcrash/battle.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/source_motion_actions.hpp"
#include <map>
#include <memory>

namespace phantomcrash {
class BattleView;
class DesktopSourceModelActions final {
public:
  DesktopSourceModelActions(const DataRoots &roots, SourceParticles &particles,
                            const std::map<int, SourceParticleDefinition> &definitions,
                            const json::JsonValue &table, float hz, float world_scale);
  void observe(const Battle &battle, const BattleView &view, float seconds);

private:
  struct Player {
    int model{-1};
    std::unique_ptr<SourceMotionActionPlayer> actions;
    Vec3 previous, displacement;
    bool ready{};
  };
  struct Effect {
    int owner{};
    SourceActionEffect definition;
    bool stopped{};
  };
  [[nodiscard]] bool alive(std::uint64_t key) const;
  [[nodiscard]] std::optional<SourceParticleParent>
  parent(const Battle &battle, const BattleView &view, const Effect &effect) const;
  std::uint64_t start(const Battle &battle, const BattleView &view, int owner,
                      const SourceActionEffect &definition);
  void update_parents(const Battle &battle, const BattleView &view);
  void retire_owner(int owner);
  SourceParticles &particles_;
  const std::map<int, SourceParticleDefinition> &definitions_;
  std::map<int, std::vector<SourceMotionAction>> banks_;
  std::map<int, Player> players_;
  std::map<std::uint64_t, Effect> effects_;
  float hz_{}, world_scale_{}, basis_scale_{1}, clock_{}, log_clock_{};
  std::uint64_t next_key_{0x51A4'0000'0000}, started_{}, death_started_{}, missing_frames_{};
};
} // namespace phantomcrash
