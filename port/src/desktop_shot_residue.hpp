#pragma once

#include "desktop_world_renderer.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/effect_bank.hpp"

#include <deque>

namespace phantomcrash {

// Impact marks and missile history belong to the battle, never to a camera.
// Their emitted world sprites are also captured by the replay archive.
class DesktopShotResidue final {
public:
  DesktopShotResidue(WorldRenderer &world, const EffectBank &bank);
  void impact(const Battle &battle, const BattleEvent &event, const ShotImpact &impact);
  void tick(const Battle &battle, float seconds);
  void add_sprites(std::vector<WorldSprite> &sprites) const;

private:
  struct Trail {
    std::deque<Vec3> points;
    Vec3 previous;
    float clock{};
    float stopped{};
    bool seen{};
  };
  void mark(const StageCollision &stage, const StageHit &hit, int kind, float size);
  const EffectBank &bank_;
  std::vector<int> textures_;
  int smoke_texture_{-1};
  std::deque<WorldSprite> marks_;
  std::map<std::uint64_t, Trail> trails_;
  float mark_rotation_{};
};

} // namespace phantomcrash
