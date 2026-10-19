#include "phantomcrash/online_socket.hpp"
#include <algorithm>
#include <memory>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif

namespace phantomcrash::online {
std::vector<Endpoint> local_broadcasts(std::uint16_t port) {
  std::vector<Endpoint> targets;
  const auto add = [&](std::uint32_t address) {
    const Endpoint endpoint{address, port};
    if (std::ranges::find(targets, endpoint) == targets.end()) {
      targets.push_back(endpoint);
    }
  };
#ifdef _WIN32
  ULONG size = 16384;
  std::vector<std::uint8_t> storage(size);
  auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(storage.data());
  auto result = GetAdaptersAddresses(
      AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
      adapters, &size);
  if (result == ERROR_BUFFER_OVERFLOW) {
    storage.resize(size);
    adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(storage.data());
    result = GetAdaptersAddresses(
        AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
        nullptr, adapters, &size);
  }
  if (result == NO_ERROR) {
    for (auto *adapter = adapters; adapter; adapter = adapter->Next) {
      if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
        continue;
      }
      for (auto *entry = adapter->FirstUnicastAddress; entry; entry = entry->Next) {
        const auto prefix = entry->OnLinkPrefixLength;
        if (entry->Address.lpSockaddr->sa_family != AF_INET || prefix == 0 || prefix >= 31) {
          continue;
        }
        const auto *address = reinterpret_cast<const sockaddr_in *>(entry->Address.lpSockaddr);
        add(ntohl(address->sin_addr.s_addr) | (0xffffffffU >> prefix));
      }
    }
  }
#else
  ifaddrs *raw{};
  if (getifaddrs(&raw) == 0) {
    const std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> interfaces(raw, freeifaddrs);
    for (auto *entry = raw; entry; entry = entry->ifa_next) {
      if (entry->ifa_broadaddr && entry->ifa_broadaddr->sa_family == AF_INET &&
          (entry->ifa_flags & IFF_UP) && (entry->ifa_flags & IFF_BROADCAST)) {
        add(ntohl(reinterpret_cast<const sockaddr_in *>(entry->ifa_broadaddr)->sin_addr.s_addr));
      }
    }
  }
#endif
  add(0xffffffffU);
  add(0x7f000001U);
  return targets;
}
} // namespace phantomcrash::online
