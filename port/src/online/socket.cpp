#include "phantomcrash/online_socket.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace phantomcrash::online {
namespace {
#ifdef _WIN32
using Socket = SOCKET;
using AddressSize = int;
constexpr Socket invalid_socket = INVALID_SOCKET;
void close_socket(Socket value) {
  closesocket(value);
}
int socket_error() {
  return WSAGetLastError();
}
bool would_block(int error) {
  return error == WSAEWOULDBLOCK;
}
struct NetworkRuntime {
  NetworkRuntime() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      throw std::runtime_error("Cannot initialize Windows networking");
    }
  }
  ~NetworkRuntime() {
    WSACleanup();
  }
};
#else
using Socket = int;
using AddressSize = socklen_t;
constexpr Socket invalid_socket = -1;
void close_socket(Socket value) {
  close(value);
}
int socket_error() {
  return errno;
}
bool would_block(int error) {
  return error == EAGAIN || error == EWOULDBLOCK;
}
struct NetworkRuntime {};
#endif

void initialize_network() {
  static NetworkRuntime runtime;
  static_cast<void>(runtime);
}

sockaddr_in address_of(Endpoint endpoint) {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(endpoint.address);
  address.sin_port = htons(endpoint.port);
  return address;
}

[[noreturn]] void fail(const char *operation) {
  throw std::runtime_error(std::string("Online ") + operation + " failed (" +
                           std::to_string(socket_error()) + ")");
}
} // namespace

struct DatagramSocket::Impl {
  Socket socket{invalid_socket};
  ~Impl() {
    if (socket != invalid_socket) {
      close_socket(socket);
    }
  }
};

Endpoint Endpoint::parse(const std::string &address, const std::uint16_t port) {
  initialize_network();
  in_addr parsed{};
  if (inet_pton(AF_INET, address.c_str(), &parsed) != 1) {
    throw std::runtime_error("Enter an IPv4 address, for example 192.168.1.10");
  }
  return {ntohl(parsed.s_addr), port};
}

std::string Endpoint::text() const {
  initialize_network();
  const auto socket_address = address_of(*this);
  std::array<char, INET_ADDRSTRLEN> buffer{};
  if (!inet_ntop(AF_INET, &socket_address.sin_addr, buffer.data(),
                 static_cast<int>(buffer.size()))) {
    fail("address formatting");
  }
  return std::string(buffer.data()) + ":" + std::to_string(port);
}

DatagramSocket::DatagramSocket(const Endpoint bind_to) : impl_(std::make_unique<Impl>()) {
  initialize_network();
  impl_->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (impl_->socket == invalid_socket) {
    fail("socket creation");
  }
#ifdef _WIN32
  u_long nonblocking = 1;
  if (ioctlsocket(impl_->socket, FIONBIO, &nonblocking) != 0) {
    fail("nonblocking socket setup");
  }
  // An expired NAT mapping can produce one ICMP reset per retransmission.
  // Do not let that error queue starve the returning peer's handshake.
  BOOL report_resets = FALSE;
  DWORD returned = 0;
  if (WSAIoctl(impl_->socket, SIO_UDP_CONNRESET, &report_resets, sizeof(report_resets), nullptr, 0,
               &returned, nullptr, nullptr) != 0) {
    fail("UDP reconnection setup");
  }
  // Prevent another process from silently stealing an active host's port.
  const BOOL exclusive = TRUE;
  if (setsockopt(impl_->socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                 reinterpret_cast<const char *>(&exclusive), sizeof(exclusive)) != 0) {
    fail("exclusive socket setup");
  }
#else
  const int flags = fcntl(impl_->socket, F_GETFL, 0);
  if (flags < 0 || fcntl(impl_->socket, F_SETFL, flags | O_NONBLOCK) < 0) {
    fail("nonblocking socket setup");
  }
#endif
  const auto address = address_of(bind_to);
  if (bind(impl_->socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
    fail("bind");
  }
}

DatagramSocket::~DatagramSocket() = default;

void DatagramSocket::enable_broadcast() {
  const int enabled = 1;
  if (setsockopt(impl_->socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&enabled),
                 sizeof(enabled)) != 0) {
    fail("broadcast setup");
  }
}

Endpoint DatagramSocket::local() const {
  sockaddr_in address{};
  AddressSize size = sizeof(address);
  if (getsockname(impl_->socket, reinterpret_cast<sockaddr *>(&address), &size) != 0) {
    fail("local address query");
  }
  return {ntohl(address.sin_addr.s_addr), ntohs(address.sin_port)};
}

bool DatagramSocket::send(const Endpoint target, std::span<const std::uint8_t> bytes) {
  if (bytes.empty() || bytes.size() > max_datagram_bytes || target.port == 0) {
    throw std::runtime_error("Invalid online datagram size or destination");
  }
  const auto address = address_of(target);
  const auto sent = sendto(impl_->socket, reinterpret_cast<const char *>(bytes.data()),
                           static_cast<int>(bytes.size()), 0,
                           reinterpret_cast<const sockaddr *>(&address), sizeof(address));
  if (sent < 0) {
    if (would_block(socket_error())) {
      return false;
    }
    fail("send");
  }
  return static_cast<std::size_t>(sent) == bytes.size();
}

bool DatagramSocket::wait_readable(const double seconds) const {
  fd_set readable;
  FD_ZERO(&readable);
  FD_SET(impl_->socket, &readable);
  const double bounded = std::max(0.0, seconds);
  timeval timeout{};
  timeout.tv_sec = static_cast<long>(bounded);
  timeout.tv_usec = static_cast<long>((bounded - static_cast<double>(timeout.tv_sec)) * 1e6);
  // The first argument is ignored on Windows.
  const int ready =
      select(static_cast<int>(impl_->socket) + 1, &readable, nullptr, nullptr, &timeout);
  return ready > 0;
}

std::optional<Datagram> DatagramSocket::receive() {
  std::array<std::uint8_t, max_datagram_bytes + 1> buffer{};
  sockaddr_in sender{};
  AddressSize size = sizeof(sender);
  const auto count =
      recvfrom(impl_->socket, reinterpret_cast<char *>(buffer.data()),
               static_cast<int>(buffer.size()), 0, reinterpret_cast<sockaddr *>(&sender), &size);
  if (count < 0) {
    const int error = socket_error();
    if (would_block(error)) {
      return std::nullopt;
    }
#ifdef _WIN32
    if (error == WSAEMSGSIZE || error == WSAECONNRESET) {
      return std::nullopt;
    }
#endif
    fail("receive");
  }
  if (count == 0 || static_cast<std::size_t>(count) > max_datagram_bytes) {
    return std::nullopt;
  }
  Datagram result;
  result.sender = {ntohl(sender.sin_addr.s_addr), ntohs(sender.sin_port)};
  result.bytes.assign(buffer.begin(), buffer.begin() + count);
  return result;
}

} // namespace phantomcrash::online
