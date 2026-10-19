#pragma once

#include "online_room.hpp"
#include "online_state.hpp"
#include <functional>
#include <map>
#include <optional>

namespace phantomcrash::online {
// Compares immutable, confirmed simulation states. Spectators participate too.
// Reports are retained until compared, independently of rollback history.
class StateChannel {
public:
  using Inspect = std::function<std::optional<StateDigest>(std::uint32_t)>;
  StateChannel(Room &room, std::uint64_t timeline, std::uint32_t first_tick);
  bool receive(const RoomMessage &message);
  void update(std::uint32_t simulated_confirmed_tick, const Inspect &inspect, double now);
  // Whether update() would check a state with `confirmed` ticks confirmed.
  [[nodiscard]] bool due(const std::uint32_t confirmed) const {
    return next_ <= confirmed;
  }
  [[nodiscard]] std::uint32_t checked_tick() const {
    return checked_;
  }
  // The first report that differed from this peer's, kept for the logs.
  struct Divergence {
    std::uint32_t tick{};
    std::uint8_t peer{};
    StateDigest local{}, remote{};
  };
  [[nodiscard]] const std::optional<Divergence> &divergence() const {
    return divergence_;
  }

private:
  using Reports = std::array<std::optional<StateDigest>, max_players>;
  void compare();
  Room &room_;
  std::uint64_t timeline_{}, next_{};
  std::uint32_t first_{}, checked_{};
  std::uint8_t peers_{};
  std::map<std::uint32_t, Reports> reports_;
  std::optional<Divergence> divergence_;
};
} // namespace phantomcrash::online
