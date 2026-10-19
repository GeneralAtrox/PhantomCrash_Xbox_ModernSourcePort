#pragma once

#include "phantomcrash/online_socket.hpp"
#include <functional>
#include <map>

namespace phantomcrash::online {

inline constexpr std::size_t max_peer_payload = max_datagram_bytes - 22;
inline constexpr std::size_t peer_window = 128;

// Reliable ordered datagrams for one established peer. Session establishment
// owns endpoint/token assignment. All times are monotonic seconds, transport
// only: wall time never enters gameplay simulation.
class ReliablePeer final {
public:
  using Send = std::function<bool(Endpoint, std::span<const std::uint8_t>)>;
  ReliablePeer(Endpoint remote, std::uint64_t token, Send send);
  void send(std::span<const std::uint8_t> payload, double now);
  // Unordered and unacknowledged: the caller repeats anything that matters.
  // False means the socket's send buffer was temporarily full.
  bool send_unsequenced(std::span<const std::uint8_t> payload);
  // Silently discard wrong-peer, wrong-session and malformed datagrams.
  [[nodiscard]] std::vector<std::vector<std::uint8_t>> receive(const Datagram &packet, double now);
  void update(double now, bool keep_retrying = false);
  void rebind(Endpoint remote) {
    remote_ = remote;
  }
  // Unsequenced payloads from datagrams passed to receive(), in arrival order.
  [[nodiscard]] std::vector<std::vector<std::uint8_t>> take_unsequenced();
  [[nodiscard]] std::size_t pending() const;
  [[nodiscard]] std::uint64_t retransmissions() const;
  // Smoothed round trip from acknowledgements of first transmissions (RFC 6298);
  // zero until one has been measured.
  [[nodiscard]] double round_trip() const {
    return smoothed_rtt_;
  }
  // Retransmission delay: smoothed round trip plus four deviations, bounded.
  [[nodiscard]] double retransmit_after() const;

private:
  struct Pending {
    std::vector<std::uint8_t> packet;
    double first{}, last{};
    bool resent{};
  };
  void acknowledge(std::uint32_t sequence);
  void measure(double sample);
  Endpoint remote_;
  std::uint64_t token_{};
  Send send_;
  std::uint32_t next_send_{1}, next_receive_{1};
  std::map<std::uint32_t, Pending> pending_;
  std::map<std::uint32_t, std::vector<std::uint8_t>> incoming_;
  std::vector<std::vector<std::uint8_t>> unsequenced_;
  std::uint64_t retransmissions_{};
  double smoothed_rtt_{}, rtt_deviation_{};
};

} // namespace phantomcrash::online
