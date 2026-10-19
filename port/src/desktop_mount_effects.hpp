#pragma once

#include "phantomcrash/battle.hpp"
#include "phantomcrash/effect_system.hpp"
#include "phantomcrash/robot_combat.hpp"

#include <array>
#include <optional>
#include <random>
#include <vector>

namespace phantomcrash {
// The electric effects on a mount whose weapon a critical hit knocked off.
// 0x08d686 puts the mount (robot +0x8c + slot * 4) in state 1 and 0x07973f
// plays it, a battle tick at a time:
//   1. 15 ticks of sparks (group 38) at the arm's forearm node and an arc
//      (39) from its shoulder joint to the forearm;
//   2. a burst (42, with 43 and 44 following) where the weapon was;
//   3. until 43 and 44 have played out;
//   4. an arm's stump keeps sparking for the rest of the battle: sparks at
//      the joint/forearm midpoint and the joint, the arcs 39 and 41, and a
//      trail (40) from the joint while slower than 350 km/h. Each restarts
//      after a random pause once it has played out.
// A shoulder weapon only has the burst.
class DesktopMountEffects final {
public:
  void tick(const Battle &battle, EffectSystem &effects, float seconds);

private:
  struct Lane {
    EffectHandle handle{};
    int wait{};  // ticks before it starts again
    Vec3 jitter; // an arc's start, in the joint's frame
  };
  using Lanes = std::array<Lane, 4>;
  struct Mount {
    int state{};
    int count{};
  };
  // The arc's two ends: the arm's shoulder joint (bone 0) and its forearm
  // (bone 1); robot +0x2fe4 / +0x2fe8 and +0x2fdc / +0x2fe0.
  struct Arm {
    Mat4 joint;
    Vec3 forearm;
  };
  struct Watch {
    const RobotCombat *owner{};
    std::array<Mount, weapon_slots> mounts{};
    // 0-1 the arms' forearms, 2-3 their joints, 4-5 the shoulder weapons
    // (robot +0x4178, four lanes a set).
    std::array<Lanes, 6> lanes{};
  };

  void step(const Combatant &robot, Watch &watch, EffectSystem &effects);
  void step_stump(std::size_t side, const Arm &arm, float speed, Watch &watch,
                  EffectSystem &effects);
  static std::optional<Arm> arm(const RobotCombat &pose, std::size_t side);
  static void stop(Lanes &lanes, EffectSystem &effects, int wait = 0);
  // Starts `group` on the lane at `at`, or moves the one playing there;
  // false once it has played out (0x086f69).
  static bool keep(Lane &lane, EffectSystem &effects, int group, Vec3 at);
  // An arc from the joint: to the forearm (39), or out to the side (41)
  // (0x087063).
  bool keep_arc(Lane &lane, EffectSystem &effects, const Arm &arm, bool side_arc, bool outward);
  [[nodiscard]] float random(float low, float high);
  [[nodiscard]] int pause(float longest, int floor);

  std::vector<Watch> robots_;
  float owed_{};
  std::minstd_rand random_{0x5eed};
};
} // namespace phantomcrash
