#pragma once

#include "phantomcrash/online_socket.hpp"

namespace phantomcrash::online {
inline constexpr std::uint16_t lan_discovery_port = 29642;
inline constexpr std::uint16_t lan_game_port = 29641;

struct LanRoom {
  std::uint64_t id{};
  Endpoint server;
  std::string name;
  std::uint8_t players{}, capacity{};
  double heard{};
};

// Discovery only advertises explicit hosts. Joining still uses Room's full
// executable/content compatibility handshake; no game state comes from discovery.
class LanAdvertiser final {
public:
  explicit LanAdvertiser(std::uint16_t game_port, Endpoint bind = {0, lan_discovery_port});
  void update(std::string name, std::uint8_t players, std::uint8_t capacity);
  [[nodiscard]] Endpoint local() const {
    return socket_.local();
  }

private:
  DatagramSocket socket_;
  std::uint16_t game_port_;
  std::uint64_t id_{};
};

class LanBrowser final {
public:
  LanBrowser();
  explicit LanBrowser(std::vector<Endpoint> targets);
  void update(double now);
  [[nodiscard]] const std::vector<LanRoom> &rooms() const {
    return rooms_;
  }
  [[nodiscard]] Endpoint local() const {
    return socket_.local();
  }

private:
  DatagramSocket socket_{{0, 0}};
  std::vector<Endpoint> targets_;
  std::vector<LanRoom> rooms_;
  std::uint64_t nonce_{};
  std::optional<double> sent_;
};
} // namespace phantomcrash::online
