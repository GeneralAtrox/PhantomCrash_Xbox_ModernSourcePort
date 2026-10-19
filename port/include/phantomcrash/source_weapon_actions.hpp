#pragma once
#include "phantomcrash/json.hpp"
#include "phantomcrash/source_particles.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace phantomcrash {
inline constexpr int source_weapon_explosion_state = 2; // actor slot +0x9c, 0x50eadc
struct SourceActionEffect {
  int part_selector{};
  int particle{-1}, alternate_particle{-1}, parent_node{-1};
  unsigned parent_flags{};
  float scale{1}, inertia_weight{}, inertia_decay{}, front_show{};
  Vec3 offset;
  SourceEffectPriority priority;
  int weapon_state_slot{-1}; // bound owner for the 0x100 state gate
};
[[nodiscard]] std::vector<SourceActionEffect>
parse_source_action_effects(const json::JsonValue &row);
struct SourceActionGate {
  unsigned mask{};
  int delay{};
};
struct SourceWeaponAction {
  unsigned flags{};
  std::vector<SourceActionGate> gates;
  std::vector<SourceActionEffect> effects;
  int weapon_state_slot{-1};
};
// Fire counters (0x40/0x60) and the per-weapon state == 2 gate (0x100).
[[nodiscard]] std::vector<SourceWeaponAction>
parse_source_weapon_actions(const json::JsonValue &bank, int owner_slot);
[[nodiscard]] Mat4 source_action_parent_frame(const Mat4 &joint, unsigned flags, Vec3 native_offset,
                                              bool blade, float source_basis_scale = 1);

class SourceWeaponActionPlayer final {
public:
  explicit SourceWeaponActionPlayer(std::vector<SourceWeaponAction> actions);
  using Start = std::function<std::uint64_t(const SourceActionEffect &)>;
  using Alive = std::function<bool(std::uint64_t)>;
  // One source update: requests reset their counters before the same update's
  // decrement. The input masks use source weapon bits 0x40/80/100/200.
  void tick(unsigned requests, unsigned permitted, const Start &start, const Alive &alive,
            const std::array<int, 4> &weapon_states = {});

private:
  struct State {
    std::array<int, 4> timers{};
    std::uint64_t last_handle{};
  };
  std::vector<SourceWeaponAction> actions_;
  std::vector<State> states_;
};
} // namespace phantomcrash
