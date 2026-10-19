#pragma once
#include "phantomcrash/online_input.hpp"
#include "phantomcrash/rumbling_entry.hpp"
#include "phantomcrash/settings.hpp"

namespace phantomcrash::online {
struct BattlePlayer {
  QuestSave profile;
  RumblingEntry entry;
};

// Common genesis for a shared arena, accepted through TimelineAgreement.
// Player slots are the host's assigned room slots, never packet-arrival order.
// This is initial construction data, not a running battle checkpoint.
struct BattleSetup {
  std::uint32_t seed{};
  int area{}, contest{}, robots{default_rumble_robots};
  QuestSave world;
  CoopOptions options;
  std::array<std::optional<BattlePlayer>, max_players> players;
  [[nodiscard]] std::uint8_t participants() const;
};

[[nodiscard]] std::vector<std::uint8_t> encode_battle_setup(const BattleSetup &setup);
[[nodiscard]] BattleSetup decode_battle_setup(std::span<const std::uint8_t> bytes);
} // namespace phantomcrash::online
