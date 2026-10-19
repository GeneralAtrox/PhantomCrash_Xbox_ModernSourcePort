#pragma once

#include "phantomcrash/online_room.hpp"
#include <cstddef>
#include <cstdint>

// Room handshake and channel constants, private to the room implementation.
namespace phantomcrash::online::room_protocol {
constexpr std::uint32_t handshake_magic = 0x484f4350; // PCOH
constexpr std::uint16_t protocol = 1;
constexpr std::uint8_t hello = 1, welcome = 2, incompatible = 3, full = 4, leaving = 5;
constexpr std::uint8_t heartbeat = 0, application = 1;
constexpr double hello_interval = 0.25, heartbeat_interval = 0.5, timeout = 10;
constexpr double drop_after = peer_drop_seconds;
constexpr std::size_t receive_budget = 64, message_limit = 512, dropped_limit = 16;
constexpr int leaving_copies = 3; // unacknowledged, so repeated against loss
} // namespace phantomcrash::online::room_protocol
