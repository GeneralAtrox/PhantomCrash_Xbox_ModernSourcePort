#pragma once
#include "phantomcrash/source_weapon_actions.hpp"

namespace phantomcrash {
struct SourceMotionClock {
  int action{-1};
  float frame{}, duration{}, speed{};
  unsigned variant{}; // original alternate selector accepts exactly 0x80 or 0x100
  int bank_mode{};    // source byte +0x41; producer is supplied independently
};
struct SourceMotionAction {
  unsigned flags{};
  std::vector<int> motions;
  float frame{};
  std::vector<SourceActionEffect> effects;
};
// Primary action entry/frame records. Original secondary/state/charge
// producers are preserved in the bank but are not substituted here.
[[nodiscard]] std::vector<SourceMotionAction>
parse_source_motion_actions(const json::JsonValue &bank);

class SourceMotionActionPlayer final {
public:
  explicit SourceMotionActionPlayer(std::vector<SourceMotionAction> actions);
  using Count = std::function<std::size_t(int)>;
  void tick(const SourceMotionClock &clock, const SourceWeaponActionPlayer::Start &start,
            const SourceWeaponActionPlayer::Alive &alive, const Count &count = {});

private:
  struct State {
    float frame{-1};
    std::uint64_t last_handle{};
  };
  std::vector<SourceMotionAction> actions_;
  std::vector<State> states_;
};
} // namespace phantomcrash
