#pragma once

#include "desktop_foot_effects.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/effect_system.hpp"
#include "phantomcrash/robot_combat.hpp"

namespace phantomcrash {
// Event-driven exhaust. Views, including live TVs, only draw the shared effect system.
class DesktopMovementEffects final {
public:
  void tick(const Battle &battle, EffectSystem &effects, float seconds);

private:
  struct Watch {
    const RobotCombat *owner{};
    bool grounded{true};
    bool descent{};
    MoveMode mode{MoveMode::stand};
    float mode_seconds{};
    std::array<std::array<EffectHandle, 3>, movement_slots> handles{};
    bool warned_missing{};
  };
  static void stop(Watch &watch, EffectSystem &effects, std::size_t first, std::size_t end);
  static int start(Watch &watch, const RobotCombat &robot, EffectSystem &effects, std::size_t slot,
                   std::size_t lane, int group, bool reflect = false);
  std::vector<Watch> robots_;
  DesktopFootEffects feet_;
};
} // namespace phantomcrash
