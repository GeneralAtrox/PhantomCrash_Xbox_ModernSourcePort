#pragma once

#include "phantomcrash/battle.hpp"
#include "phantomcrash/effect_system.hpp"
#include "phantomcrash/robot_combat.hpp"

namespace phantomcrash {
class DesktopFootEffects final {
public:
  void tick(const Battle &battle, EffectSystem &effects, float seconds);

private:
  struct Contact {
    PoseContact previous;
    EffectHandle takeoff{};
    float remaining{};
  };
  struct Watch {
    const RobotCombat *owner{};
    bool grounded{true};
    std::vector<Contact> contacts;
  };
  static void stop(Watch &watch, EffectSystem &effects);
  std::vector<Watch> robots_;
};
} // namespace phantomcrash
