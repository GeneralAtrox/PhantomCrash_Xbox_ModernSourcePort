#include "test_support.hpp"
#include "phantomcrash/online_peer.hpp"
#include <algorithm>
#include <array>

using namespace phantomcrash::online;

TEST_CASE("online peer recovers lost data and acknowledgements without duplicate delivery") {
  const Endpoint host{0x7f000001, 31001}, client{0x7f000001, 31002};
  std::vector<Datagram> to_host, to_client;
  ReliablePeer sender(client, 77, [&](Endpoint target, std::span<const std::uint8_t> bytes) {
    CHECK(target == client);
    to_client.push_back({host, {bytes.begin(), bytes.end()}});
    return true;
  });
  ReliablePeer receiver(host, 77, [&](Endpoint target, std::span<const std::uint8_t> bytes) {
    CHECK(target == host);
    to_host.push_back({client, {bytes.begin(), bytes.end()}});
    return true;
  });
  for (std::uint8_t i = 1; i <= 3; ++i) {
    sender.send(std::array<std::uint8_t, 1>{i}, 0);
  }
  CHECK(sender.pending() == 3);
  // Lose sequence 1, then deliver 3 before 2. Neither can be applied yet.
  CHECK(receiver.receive(to_client[2], 0.01).empty());
  CHECK(receiver.receive(to_client[1], 0.01).empty());
  to_client.clear();
  // Lose both acknowledgements too; all three will be retransmitted.
  to_host.clear();
  sender.update(0.06);
  CHECK(sender.retransmissions() == 3);
  std::vector<int> delivered;
  for (const auto &datagram : to_client) {
    for (const auto &message : receiver.receive(datagram, 0.07)) {
      delivered.push_back(message.at(0));
    }
    CHECK(receiver.receive(datagram, 0.08).empty());
  }
  CHECK(delivered == (std::vector<int>{1, 2, 3}));
  for (const auto &datagram : to_host) {
    CHECK(sender.receive(datagram, 0.09).empty());
  }
  CHECK(sender.pending() == 0);
}

TEST_CASE("online peer ignores other endpoints and sessions before acknowledging") {
  const Endpoint host{0x7f000001, 31001}, client{0x7f000001, 31002};
  std::vector<Datagram> outbound;
  int acknowledgements = 0;
  ReliablePeer sender(client, 77, [&](Endpoint, std::span<const std::uint8_t> bytes) {
    outbound.push_back({host, {bytes.begin(), bytes.end()}});
    return true;
  });
  ReliablePeer receiver(host, 88, [&](Endpoint, std::span<const std::uint8_t>) {
    ++acknowledgements;
    return true;
  });
  sender.send(std::array<std::uint8_t, 1>{9}, 0);
  CHECK(receiver.receive(outbound[0], 0).empty());
  CHECK(acknowledgements == 0);
  outbound[0].sender = client;
  CHECK(receiver.receive(outbound[0], 0).empty());
  CHECK(acknowledgements == 0);
}

TEST_CASE("online peer bounds unacknowledged data and reports timeout") {
  ReliablePeer peer({0x7f000001, 31001}, 1,
                    [](Endpoint, std::span<const std::uint8_t>) { return true; });
  peer.send(std::array<std::uint8_t, 1>{1}, 0);
  bool timed_out = false;
  try {
    peer.update(10);
  } catch (const std::runtime_error &) {
    timed_out = true;
  }
  CHECK(timed_out);
}

TEST_CASE("online peer measures round trips and paces retransmission by them") {
  const Endpoint host{0x7f000001, 31001}, client{0x7f000001, 31002};
  std::vector<Datagram> to_host, to_client;
  ReliablePeer sender(client, 77, [&](Endpoint, std::span<const std::uint8_t> bytes) {
    to_client.push_back({host, {bytes.begin(), bytes.end()}});
    return true;
  });
  ReliablePeer receiver(host, 77, [&](Endpoint, std::span<const std::uint8_t> bytes) {
    to_host.push_back({client, {bytes.begin(), bytes.end()}});
    return true;
  });
  CHECK(sender.round_trip() == 0.0);
  CHECK(sender.retransmit_after() == 0.05);
  for (int message = 0; message < 8; ++message) {
    const double sent = message * 1.0;
    sender.send(std::array<std::uint8_t, 1>{static_cast<std::uint8_t>(message + 1)}, sent);
    CHECK(receiver.receive(to_client.back(), sent + 0.1).size() == 1);
    CHECK(sender.receive(to_host.back(), sent + 0.2).empty());
  }
  CHECK(sender.round_trip() > 0.19);
  CHECK(sender.round_trip() < 0.21);
  // A 200 ms route waits longer than the unmeasured default before resending.
  CHECK(sender.retransmit_after() > 0.2);
  sender.send(std::array<std::uint8_t, 1>{9}, 10);
  sender.update(10.1);
  CHECK(sender.retransmissions() == 0);
  sender.update(10.5);
  CHECK(sender.retransmissions() == 1);
  // Karn's rule: the retransmitted message's acknowledgement is not a sample.
  const double before = sender.round_trip();
  CHECK(receiver.receive(to_client.back(), 12).size() == 1);
  CHECK(sender.receive(to_host.back(), 12).empty());
  CHECK(sender.round_trip() == before);
  CHECK(sender.pending() == 0);
}

TEST_CASE("online peer delivers unsequenced payloads at once and never acknowledges them") {
  const Endpoint host{0x7f000001, 31001}, client{0x7f000001, 31002};
  std::vector<Datagram> to_client;
  int acknowledgements = 0;
  ReliablePeer sender(client, 77, [&](Endpoint, std::span<const std::uint8_t> bytes) {
    to_client.push_back({host, {bytes.begin(), bytes.end()}});
    return true;
  });
  ReliablePeer receiver(host, 77, [&](Endpoint, std::span<const std::uint8_t>) {
    ++acknowledgements;
    return true;
  });
  // A reliable message lost ahead of it does not hold an unsequenced one back.
  sender.send(std::array<std::uint8_t, 1>{1}, 0);
  to_client.clear();
  CHECK(sender.send_unsequenced(std::array<std::uint8_t, 2>{5, 6}));
  CHECK(receiver.receive(to_client.back(), 0).empty());
  const auto unsequenced = receiver.take_unsequenced();
  REQUIRE(unsequenced.size() == 1);
  CHECK(unsequenced[0] == (std::vector<std::uint8_t>{5, 6}));
  CHECK(acknowledgements == 0);
  CHECK(sender.pending() == 1);
  CHECK(receiver.take_unsequenced().empty());
}
