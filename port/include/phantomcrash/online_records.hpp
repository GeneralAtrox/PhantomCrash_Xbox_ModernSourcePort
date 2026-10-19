#pragma once
#include "phantomcrash/online_room.hpp"
#include <deque>

namespace phantomcrash::online {
inline constexpr std::size_t max_record_bytes = 8 * 1024 * 1024;

struct Record {
  std::uint8_t sender{};
  std::uint16_t kind{};
  std::vector<std::uint8_t> bytes;
};

// Reliable room datagrams carrying a larger startup/checkpoint record. Sending
// is incremental and leaves space for input and heartbeat packets. One ordered
// transfer per peer is assembled at a time; all storage is bounded.
class RecordChannel final {
public:
  explicit RecordChannel(Room &room) : room_(room) {}
  void send(std::uint8_t recipient, std::uint16_t kind, std::vector<std::uint8_t> bytes);
  void update(double now);
  bool receive(const RoomMessage &message);
  [[nodiscard]] std::vector<Record> take_records();
  [[nodiscard]] bool pending() const;

private:
  struct Outgoing {
    std::uint64_t id{};
    std::uint16_t kind{};
    std::vector<std::uint8_t> bytes;
    std::size_t offset{};
  };
  struct Incoming {
    std::uint64_t id{}, completed{};
    std::uint16_t kind{};
    std::uint32_t total{};
    std::vector<std::uint8_t> bytes;
  };
  Room &room_;
  std::uint64_t next_{1};
  std::array<std::deque<Outgoing>, max_players> outgoing_;
  std::array<Incoming, max_players> incoming_;
  std::vector<Record> records_;
};
} // namespace phantomcrash::online
