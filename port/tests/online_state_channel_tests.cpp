#include "test_support.hpp"
#include "phantomcrash/online_state_channel.hpp"
#include "../src/online/wire.hpp"
#include <chrono>
#include <thread>

namespace net = phantomcrash::online;
namespace {
struct Fixture {
  std::array<std::unique_ptr<net::Room>, 3> rooms;
  std::array<std::unique_ptr<net::StateChannel>, 3> states;
  double now{};
  Fixture() {
    net::RoomOptions options;
    options.host = true;
    options.bind = net::Endpoint::parse("127.0.0.1", 0);
    rooms[0] = std::make_unique<net::Room>(options);
    options.host = false;
    options.server = rooms[0]->local_endpoint();
    for (int i = 1; i < 3; ++i) {
      rooms[i] = std::make_unique<net::Room>(options);
    }
    for (int pass = 0; pass < 40; ++pass) {
      for (auto &room : rooms) {
        room->update(now);
      }
      now += 0.001;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (int i = 0; i < 3; ++i) {
      CHECK(rooms[i]->connected() && rooms[i]->participants() == 7);
      states[i] = std::make_unique<net::StateChannel>(*rooms[i], 9, 17);
    }
  }
  void pump() {
    for (int pass = 0; pass < 12; ++pass) {
      for (int i = 0; i < 3; ++i) {
        rooms[i]->update(now);
        for (const auto &message : rooms[i]->take_messages()) {
          CHECK(states[i]->receive(message));
        }
      }
      now += 0.001;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
};
std::optional<net::StateDigest> digest(const std::uint32_t tick) {
  net::StateDigest result{};
  result[0] = static_cast<std::uint8_t>(tick);
  result[1] = static_cast<std::uint8_t>(tick >> 8);
  return result;
}
} // namespace

TEST_CASE("confirmed state reports include spectators and retain delayed comparison history") {
  Fixture f;
  // The third peer catches up after the first two have passed rollback history.
  for (int i = 0; i < 2; ++i) {
    f.states[i]->update(257, digest, f.now);
  }
  f.pump();
  CHECK(f.states[0]->checked_tick() == 17);
  CHECK(f.states[1]->checked_tick() == 257);
  CHECK(f.states[2]->checked_tick() == 17);
  f.states[2]->update(257, digest, f.now);
  f.pump();
  for (const auto &state : f.states) {
    CHECK(state->checked_tick() == 257);
  }
  // No unconfirmed tick is inspected, and an unchanged boundary emits nothing.
  int inspections = 0;
  f.states[0]->update(
      316,
      [&](auto tick) {
        ++inspections;
        return digest(tick);
      },
      f.now);
  CHECK(inspections == 0);
  const auto divergent = [](const auto tick) {
    auto result = digest(tick);
    (*result)[5] = 1;
    return result;
  };
  f.states[0]->update(317, digest, f.now);
  f.states[1]->update(317, digest, f.now);
  f.states[2]->update(317, divergent, f.now);
  CHECK_THROWS(f.pump());
}

TEST_CASE("confirmed state protocol validates timeline sender tick and bounded backlog") {
  Fixture f;
  const auto packet = [](const std::uint64_t timeline, const std::uint32_t tick) {
    net::WireWriter out;
    out.u32(0x534f4350);
    out.u16(1);
    out.u64(timeline);
    out.u32(tick);
    out.raw(*digest(tick));
    return out.take();
  };
  CHECK(!f.states[0]->receive({1, packet(10, 77)}));
  CHECK_THROWS(f.states[0]->receive({0, packet(9, 77)}));
  CHECK_THROWS(f.states[1]->receive({2, packet(9, 77)}));
  CHECK_THROWS(f.states[0]->receive({1, packet(9, 78)}));
  CHECK_THROWS(f.states[0]->receive({1, packet(9, 17 + 60 * 100)}));
  CHECK_THROWS(
      f.states[0]->update(77, [](auto) { return std::optional<net::StateDigest>{}; }, f.now));
  net::StateChannel backlog(*f.rooms[0], 11, 0);
  CHECK_THROWS(backlog.update(60 * 17, digest, f.now));
}
