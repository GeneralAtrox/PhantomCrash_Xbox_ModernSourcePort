#include "phantomcrash/online_records.hpp"
#include "wire.hpp"
#include <algorithm>
#include <limits>
#include <utility>

namespace phantomcrash::online {
namespace {
constexpr std::uint32_t magic = 0x524f4350; // PCOR
constexpr std::uint16_t version = 1;
constexpr std::size_t header_bytes = 26;
constexpr std::size_t fragment_bytes = max_peer_payload - 1 - header_bytes;
constexpr std::size_t reserved_packets = 32;
constexpr std::size_t records_per_peer = 16;
} // namespace

void RecordChannel::send(const std::uint8_t recipient, const std::uint16_t kind,
                         std::vector<std::uint8_t> bytes) {
  if (recipient >= max_players || recipient == room_.local_player() ||
      !(room_.participants() & (1U << recipient)) ||
      (room_.local_player() != 0 && recipient != 0) || bytes.size() > max_record_bytes ||
      next_ == std::numeric_limits<std::uint64_t>::max()) {
    throw std::runtime_error("Online record recipient or size is invalid");
  }
  auto &queue = outgoing_[recipient];
  std::size_t queued = bytes.size();
  for (const auto &record : queue) {
    queued += record.bytes.size();
  }
  if (queue.size() >= records_per_peer || queued > max_record_bytes) {
    throw std::runtime_error("Online record queue is full");
  }
  queue.push_back({next_++, kind, std::move(bytes), 0});
}

void RecordChannel::update(const double now) {
  for (std::uint8_t peer = 0; peer < max_players; ++peer) {
    auto &queue = outgoing_[peer];
    if (!(room_.participants() & (1U << peer))) {
      queue.clear(); // a departed peer's records are not delivered
      continue;
    }
    for (int budget = 0;
         budget < 8 && !queue.empty() && room_.send_capacity(peer) > reserved_packets; ++budget) {
      auto &record = queue.front();
      const auto count = std::min(fragment_bytes, record.bytes.size() - record.offset);
      WireWriter out;
      out.u32(magic);
      out.u16(version);
      out.u16(record.kind);
      out.u64(record.id);
      out.u32(static_cast<std::uint32_t>(record.bytes.size()));
      out.u32(static_cast<std::uint32_t>(record.offset));
      out.u16(static_cast<std::uint16_t>(count));
      out.raw(std::span(record.bytes).subspan(record.offset, count));
      room_.send(peer, out.take(), now);
      record.offset += count;
      if (record.offset == record.bytes.size()) {
        queue.pop_front();
      }
    }
  }
}

bool RecordChannel::receive(const RoomMessage &message) {
  if (message.bytes.size() < 4) {
    return false;
  }
  WireReader in(message.bytes);
  if (in.u32() != magic) {
    return false;
  }
  if (in.u16() != version || message.sender >= max_players) {
    throw std::runtime_error("Unsupported online record header");
  }
  const auto kind = in.u16();
  const auto id = in.u64();
  const auto total = in.u32();
  const auto offset = in.u32();
  const auto count = in.u16();
  const auto bytes = in.raw(count);
  in.finish();
  if (!id || total > max_record_bytes || offset > total || count > total - offset ||
      count > fragment_bytes || (count == 0 && total != 0)) {
    throw std::runtime_error("Invalid online record fragment");
  }
  auto &record = incoming_[message.sender];
  if (!record.id) {
    if (offset || id <= record.completed) {
      throw std::runtime_error("Online record does not begin at its first fragment");
    }
    record.id = id;
    record.kind = kind;
    record.total = total;
    record.bytes.clear();
    record.bytes.reserve(total);
  }
  if (record.id != id || record.kind != kind || record.total != total ||
      record.bytes.size() != offset) {
    throw std::runtime_error("Online record fragments do not belong to the same ordered transfer");
  }
  record.bytes.insert(record.bytes.end(), bytes.begin(), bytes.end());
  if (record.bytes.size() == total) {
    if (records_.size() >= records_per_peer) {
      throw std::runtime_error("Online application is not consuming complete records");
    }
    records_.push_back({message.sender, kind, std::move(record.bytes)});
    record.completed = id;
    record.id = 0;
  }
  return true;
}

std::vector<Record> RecordChannel::take_records() {
  return std::exchange(records_, {});
}

bool RecordChannel::pending() const {
  return std::ranges::any_of(outgoing_, [](const auto &queue) { return !queue.empty(); });
}
} // namespace phantomcrash::online
