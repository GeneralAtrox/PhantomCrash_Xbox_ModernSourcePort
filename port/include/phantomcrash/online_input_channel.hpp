#pragma once

#include "phantomcrash/online_prediction.hpp"
#include "phantomcrash/online_room.hpp"
#include <deque>
#include <set>

namespace phantomcrash::online {

// Routes a match's tick inputs through its room host. The host validates the
// assigned sender before relaying; a client can only author its own controls.
// The owner supplies a new nonzero timeline ID for each host-ordered match start.
//
// Controls travel as unsequenced datagrams, never behind a lost packet: each
// one carries every control the receiver has not acknowledged (the oldest
// first, the newest always) and this side's acknowledgements. A control is
// repeated until acknowledged, so the stream is reliable without ordering.
class InputChannel final {
public:
  InputChannel(Room &room, Prediction &prediction, std::uint64_t timeline, std::uint8_t players);
  void submit(const PlayerInput &input, double now);
  // False: not this timeline's input message; leave it to the room dispatcher.
  // Invalid messages for this timeline throw and must terminate that timeline.
  bool receive(const RoomMessage &message, double now);
  // Repeats unacknowledged controls, and owed acknowledgements, while no new
  // tick is sending them (a prediction stall, or a peer that missed packets).
  void update(double now);
  // Host only: idle controls for players whose computers left the room, so the
  // others keep confirming ticks until each departure is settled.
  void stand_in(double now);
  // Controls not yet acknowledged by that peer, for diagnostics and tests.
  [[nodiscard]] std::size_t unacknowledged(std::uint8_t peer) const;

private:
  struct Sent {
    std::uint32_t tick{};
    std::vector<std::uint8_t> bytes;
  };
  struct Link {
    std::array<std::deque<Sent>, max_players> unacknowledged;
    std::optional<double> sent;
    bool acknowledgement_due{};
  };
  void queue(std::uint8_t peer, const PlayerInput &input, const std::vector<std::uint8_t> &bytes);
  void flush(std::uint8_t peer, double now);
  void received(const PlayerInput &input);
  void acknowledged(Link &link, std::span<const std::uint32_t> next);
  [[nodiscard]] std::uint32_t next_needed(std::uint8_t player) const;
  [[nodiscard]] bool is_peer(std::uint8_t peer) const;
  Room &room_;
  Prediction &prediction_;
  std::uint64_t timeline_{};
  std::uint8_t players_{};
  std::optional<PlayerInput> submitted_;
  std::uint8_t peers_{};
  std::array<Link, max_players> links_;
  // Per author: the first tick not yet received, and later ticks already here.
  std::array<std::uint32_t, max_players> contiguous_{};
  std::array<std::set<std::uint32_t>, max_players> ahead_;
};

} // namespace phantomcrash::online
