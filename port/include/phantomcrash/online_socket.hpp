#pragma once

#include <cstdint>
#include <compare>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace phantomcrash::online {

inline constexpr std::size_t max_datagram_bytes = 1200;

struct Endpoint {
  // IPv4 in host byte order; zero binds all interfaces.
  std::uint32_t address{};
  std::uint16_t port{};
  auto operator<=>(const Endpoint &) const = default;
  [[nodiscard]] static Endpoint parse(const std::string &address, std::uint16_t port);
  [[nodiscard]] std::string text() const;
};

struct Datagram {
  Endpoint sender;
  std::vector<std::uint8_t> bytes;
};

[[nodiscard]] std::vector<Endpoint> local_broadcasts(std::uint16_t port);

// Nonblocking, one owned socket; no DNS lookup or global socket handles.
class DatagramSocket final {
public:
  explicit DatagramSocket(Endpoint bind_to);
  ~DatagramSocket();
  DatagramSocket(const DatagramSocket &) = delete;
  DatagramSocket &operator=(const DatagramSocket &) = delete;
  [[nodiscard]] Endpoint local() const;
  void enable_broadcast();
  // False means the send buffer is temporarily full. Callers retain reliable data.
  bool send(Endpoint target, std::span<const std::uint8_t> bytes);
  [[nodiscard]] std::optional<Datagram> receive();
  // Blocks until a datagram is waiting or `seconds` pass; true if one is waiting.
  // Safe to call from one thread while another sends or receives.
  bool wait_readable(double seconds) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace phantomcrash::online
