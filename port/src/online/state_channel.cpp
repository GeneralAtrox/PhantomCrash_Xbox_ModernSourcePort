#include "phantomcrash/online_state_channel.hpp"
#include "wire.hpp"
#include <algorithm>

namespace phantomcrash::online {
namespace {
constexpr std::uint32_t state_message = 0x534f4350; // PCOS
constexpr std::uint16_t version = 1;
constexpr std::uint32_t interval = 60;
constexpr std::size_t maximum_pending = 16;
} // namespace

StateChannel::StateChannel(Room &room, const std::uint64_t timeline, const std::uint32_t first_tick)
    : room_(room), timeline_(timeline), next_(std::uint64_t(first_tick) + interval),
      first_(first_tick), checked_(first_tick),
      peers_(room.local_player() == 0 ? room.participants() : (1U | (1U << room.local_player()))) {
  if (!room.connected() || !timeline) {
    throw std::runtime_error("Invalid online state channel");
  }
}

bool StateChannel::receive(const RoomMessage &message) {
  if (message.bytes.size() < 4) {
    return false;
  }
  WireReader in(message.bytes);
  if (in.u32() != state_message) {
    return false;
  }
  if (in.u16() != version) {
    throw std::runtime_error("Unsupported online state protocol");
  }
  if (in.u64() != timeline_) {
    return false;
  }
  const auto tick = in.u32();
  StateDigest digest;
  const auto bytes = in.raw(digest.size());
  std::copy(bytes.begin(), bytes.end(), digest.begin());
  in.finish();
  if (message.sender >= max_players || message.sender == room_.local_player() ||
      !(peers_ & (1U << message.sender)) || tick <= first_ || (tick - first_) % interval ||
      std::uint64_t(tick) > next_ + interval * maximum_pending) {
    throw std::runtime_error("Invalid online confirmed-state report");
  }
  if (tick <= checked_) {
    return true;
  }
  auto &report = reports_[tick][message.sender];
  if (report && *report != digest) {
    throw std::runtime_error("Peer replaced its confirmed-state report");
  }
  report = digest;
  compare();
  return true;
}

void StateChannel::update(const std::uint32_t confirmed, const Inspect &inspect, const double now) {
  while (next_ <= confirmed) {
    const auto tick = static_cast<std::uint32_t>(next_);
    const auto digest = inspect(tick);
    if (!digest) {
      throw std::runtime_error("Online state check missed retained confirmed history");
    }
    reports_[tick][room_.local_player()] = *digest;
    WireWriter out;
    out.u32(state_message);
    out.u16(version);
    out.u64(timeline_);
    out.u32(tick);
    out.raw(*digest);
    const auto bytes = out.take();
    for (std::uint8_t peer = 0; peer < max_players; ++peer) {
      if (peer != room_.local_player() && (peers_ & room_.participants() & (1U << peer))) {
        room_.send(peer, bytes, now);
      }
    }
    next_ += interval;
    compare();
  }
}

void StateChannel::compare() {
  while (!reports_.empty()) {
    const auto &[tick, reports] = *reports_.begin();
    const auto &local = reports[room_.local_player()];
    bool complete = local.has_value();
    for (int peer = 0; peer < max_players; ++peer) {
      if (!(peers_ & (1U << peer))) {
        continue;
      }
      // A departed peer's reports end; the remaining ones are still compared.
      complete &= reports[peer].has_value() || !(room_.participants() & (1U << peer));
      if (local && reports[peer] && *local != *reports[peer]) {
        divergence_ = Divergence{tick, static_cast<std::uint8_t>(peer), *local, *reports[peer]};
        throw std::runtime_error("Online gameplay diverged: timeline=" + std::to_string(timeline_) +
                                 " tick=" + std::to_string(tick) +
                                 " player=" + std::to_string(peer + 1));
      }
    }
    if (!complete) {
      break;
    }
    checked_ = tick;
    reports_.erase(reports_.begin());
  }
  if (reports_.size() > maximum_pending) {
    throw std::runtime_error("Online confirmed-state comparison fell behind");
  }
}
} // namespace phantomcrash::online
