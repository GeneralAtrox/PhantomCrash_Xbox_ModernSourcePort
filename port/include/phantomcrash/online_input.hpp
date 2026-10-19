#pragma once

#include "phantomcrash/battle.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace phantomcrash::online {

inline constexpr std::uint8_t max_players = 4;
inline constexpr std::size_t max_input_bytes = 136;
inline constexpr float tick_seconds = 1.0F / 60.0F;

// Captured after local control mapping and before the shared simulation step.
// The local HUD owns tracking; all replicas consume these same view values.
struct PlayerInput {
  std::uint32_t tick{};
  std::uint8_t player{};
  RobotControl control;
  FireControl fire;
  std::optional<TargetView> view;
};

// Explicit little-endian wire encoding, with lossless IEEE float bits.
// Invalid values, truncated records and trailing bytes throw std::runtime_error.
[[nodiscard]] std::vector<std::uint8_t> encode_input(const PlayerInput &input);
[[nodiscard]] PlayerInput decode_input(std::span<const std::uint8_t> bytes);
[[nodiscard]] bool same_input(const PlayerInput &a, const PlayerInput &b);

// Only held controls persist across an unconfirmed remote tick. Mouse movement
// and pressed-button edges must never be applied twice by prediction.
[[nodiscard]] PlayerInput predict_input(const PlayerInput &previous, std::uint32_t tick);

} // namespace phantomcrash::online
