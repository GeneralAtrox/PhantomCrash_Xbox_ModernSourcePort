#include "phantomcrash/online_room.hpp"
#include "room_protocol.hpp"
#include <array>

// Departures: a quitting player's notice, and dropping a peer silent for good.
namespace phantomcrash::online {
using namespace room_protocol;

Room::~Room() {
  // A player who quits tells the others at once instead of leaving them to
  // wait out the drop delay.
  try {
    const std::lock_guard lock(mutex_);
    if (!error_.empty() || local_player_ >= max_players) {
      return;
    }
    for (int copy = 0; copy < leaving_copies; ++copy) {
      if (!options_.host) {
        send_handshake(options_.server, leaving, local_player_, nonce_);
        continue;
      }
      for (const auto &peer : peers_) {
        if (peer.channel) {
          send_handshake(peer.endpoint, leaving, 0, peer.nonce);
        }
      }
    }
  } catch (...) {
    // Closing must not fail; the others drop a silent peer anyway.
  }
}

void Room::drop(const std::uint8_t slot, const double now) {
  dropped_.push_back(peers_[slot].nonce);
  if (dropped_.size() > dropped_limit) {
    dropped_.erase(dropped_.begin());
  }
  peers_[slot] = {};
  for (auto &peer : peers_) {
    if (peer.ready && peer.channel->pending() < peer_window) {
      peer.channel->send(std::array<std::uint8_t, 2>{heartbeat, participants()}, now);
    }
  }
}

bool Room::drop_silent(const double now) {
  for (std::uint8_t slot = 0; slot < max_players; ++slot) {
    if (peers_[slot].channel && now - peers_[slot].heard >= drop_after) {
      if (!options_.host) {
        error_ = "Lost contact with the online host";
        return false;
      }
      drop(slot, now);
    }
  }
  return true;
}
} // namespace phantomcrash::online
