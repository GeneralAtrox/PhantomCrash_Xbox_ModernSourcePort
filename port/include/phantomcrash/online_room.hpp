#pragma once

#include "phantomcrash/online_peer.hpp"
#include "phantomcrash/online_input.hpp"
#include <array>
#include <mutex>

namespace phantomcrash::online {

using Compatibility = std::array<std::uint8_t, 32>;

// Recovery waits out shorter outages; a peer silent this long has left for good.
inline constexpr double peer_drop_seconds = 30;

struct RoomOptions {
  bool host{};
  Endpoint bind;
  Endpoint server;
  Compatibility compatibility{}; // caller's executable/gameplay-content digest
  std::uint8_t capacity{3};
  std::uint32_t seed{1}; // used by host only
};

struct RoomMessage {
  std::uint8_t sender{}; // assigned by handshake, never trusted from the payload
  std::vector<std::uint8_t> bytes;
};

// Session establishment and bounded reliable messages. This owns networking,
// not the game clock. A room can stay connected while players browse menus.
// Every member is safe to call from a transport worker and the game thread.
class Room final {
public:
  explicit Room(RoomOptions options);
  ~Room();
  Room(const Room &) = delete;
  Room &operator=(const Room &) = delete;
  void update(double now);
  void send(std::uint8_t recipient, std::span<const std::uint8_t> bytes, double now);
  // One unordered, unacknowledged datagram; the caller repeats what matters.
  void send_unsequenced(std::uint8_t recipient, std::span<const std::uint8_t> bytes);
  void broadcast(std::span<const std::uint8_t> bytes, double now);
  // The reliable channel's measured retransmission delay to that peer.
  [[nodiscard]] double retransmit_after(std::uint8_t recipient) const;
  [[nodiscard]] double round_trip(std::uint8_t recipient) const;
  // Blocks until a datagram is waiting or `seconds` pass. Call without other work held.
  void wait(double seconds) const;
  [[nodiscard]] std::size_t send_capacity(std::uint8_t recipient) const;
  [[nodiscard]] std::vector<RoomMessage> take_messages();
  [[nodiscard]] std::uint8_t local_player() const;
  [[nodiscard]] std::uint8_t participants() const;
  [[nodiscard]] std::uint32_t seed() const;
  [[nodiscard]] bool connected() const;
  [[nodiscard]] bool recovering() const;
  [[nodiscard]] Endpoint local_endpoint() const;
  [[nodiscard]] std::string error() const;

private:
  struct Peer {
    Endpoint endpoint;
    std::uint64_t nonce{};
    std::unique_ptr<ReliablePeer> channel;
    double heard{}, heartbeat{};
    bool ready{};
  };
  bool handshake(const Datagram &packet, double now);
  void send_handshake(Endpoint target, std::uint8_t kind, std::uint8_t slot, std::uint64_t nonce);
  void receive(Peer &peer, std::uint8_t slot, const Datagram &packet, double now);
  [[nodiscard]] std::unique_ptr<ReliablePeer> channel(Endpoint remote);
  void deliver(std::uint8_t slot, std::span<const std::uint8_t> payload, bool unsequenced);
  // Host only: frees a departed client's slot and tells the others.
  void drop(std::uint8_t slot, double now);
  // False once a client has lost its host for good (error_ is set).
  bool drop_silent(double now);
  // Recursive: public members call each other (broadcast -> send, update -> participants).
  mutable std::recursive_mutex mutex_;
  RoomOptions options_;
  DatagramSocket socket_;
  std::uint64_t nonce_{}, token_{};
  std::uint8_t local_player_{max_players};
  std::uint8_t membership_{}; // host's participant mask, received by clients
  std::array<Peer, max_players> peers_;
  std::vector<RoomMessage> messages_;
  std::vector<std::uint64_t> dropped_; // session nonces of departed clients
  std::optional<double> started_, hello_;
  std::string error_;
  double now_{};
};

} // namespace phantomcrash::online
