#include "test_support.hpp"
#include "phantomcrash/online_room.hpp"
#include <chrono>
#include <thread>

using namespace phantomcrash::online;

namespace {
void pump(Room &host, Room &client, double &now) {
  for (int i = 0; i < 20; ++i) {
    host.update(now);
    client.update(now);
    now += 0.005;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
RoomOptions host_options() {
  RoomOptions options;
  options.host = true;
  options.bind = Endpoint::parse("127.0.0.1", 0);
  options.seed = 12345;
  options.compatibility[0] = 7;
  return options;
}
} // namespace

TEST_CASE("online room assigns identity and seed then exchanges application messages") {
  auto options = host_options();
  Room host(options);
  options.host = false;
  options.server = host.local_endpoint();
  options.seed = 0;
  Room client(options);
  double now = 0;
  pump(host, client, now);
  CHECK(host.error().empty() && client.error().empty());
  CHECK(client.connected() && client.local_player() == 1);
  CHECK(host.participants() == 3 && client.participants() == 3);
  CHECK(client.seed() == host.seed() && client.seed() == 12345);
  const std::array<std::uint8_t, 3> payload{12, 34, 56};
  client.send(0, payload, now);
  pump(host, client, now);
  const auto messages = host.take_messages();
  REQUIRE(messages.size() == 1);
  CHECK(messages[0].sender == 1);
  CHECK(messages[0].bytes == (std::vector<std::uint8_t>{12, 34, 56}));
  host.broadcast(messages[0].bytes, now);
  pump(host, client, now);
  const auto replies = client.take_messages();
  REQUIRE(replies.size() == 1);
  CHECK(replies[0].sender == 0 && replies[0].bytes == messages[0].bytes);
}

TEST_CASE("online room rejects mismatched gameplay content before admitting player") {
  auto options = host_options();
  Room host(options);
  options.host = false;
  options.server = host.local_endpoint();
  options.compatibility[0] = 8;
  Room client(options);
  double now = 0;
  pump(host, client, now);
  CHECK(!client.connected());
  CHECK(client.error().find("different game build") != std::string::npos);
  CHECK(host.participants() == 1);
}

TEST_CASE("online room shares all three assigned player slots") {
  auto options = host_options();
  Room host(options);
  options.host = false;
  options.server = host.local_endpoint();
  Room first(options), second(options);
  double now = 0;
  pump(host, first, now);
  pump(host, second, now);
  pump(host, first, now);
  CHECK(first.local_player() == 1 && second.local_player() == 2);
  CHECK(host.participants() == 7 && first.participants() == 7 && second.participants() == 7);
}

TEST_CASE("online room resumes the same slot after a network outage and endpoint change") {
  auto options = host_options();
  Room host(options);
  DatagramSocket front(Endpoint::parse("127.0.0.1", 0));
  auto back = std::make_unique<DatagramSocket>(Endpoint::parse("127.0.0.1", 0));
  options.host = false;
  options.server = front.local();
  Room client(options);
  Endpoint client_address;
  double now = 0;
  const auto route = [&](const bool drop) {
    while (auto packet = front.receive()) {
      client_address = packet->sender;
      if (!drop) {
        static_cast<void>(back->send(host.local_endpoint(), packet->bytes));
      }
    }
    while (auto packet = back->receive()) {
      if (!drop && client_address.port) {
        static_cast<void>(front.send(client_address, packet->bytes));
      }
    }
  };
  const auto progress = [&](const int count, const bool drop) {
    for (int pass = 0; pass < count; ++pass) {
      route(drop);
      host.update(now);
      client.update(now);
      route(drop);
      now += 0.05;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  progress(40, false);
  REQUIRE(client.connected());
  CHECK(client.local_player() == 1);
  client.send(0, std::array<std::uint8_t, 1>{42}, now);
  progress(260, true); // Exceeds the old fatal ten-second transport timeout.
  CHECK(host.recovering() && client.recovering());
  CHECK(host.error().empty() && client.error().empty());
  back = std::make_unique<DatagramSocket>(Endpoint::parse("127.0.0.1", 0));
  progress(100, false); // The client's repeated nonce rebinds the host endpoint.
  CHECK(host.error().empty());
  CHECK(client.error().empty());
  CHECK(!host.recovering() && !client.recovering());
  CHECK(host.participants() == 3 && client.local_player() == 1);
  const auto messages = host.take_messages();
  REQUIRE(messages.size() == 1);
  CHECK(messages.front().bytes == std::vector<std::uint8_t>{42});
}

TEST_CASE("online room frees a quitting player's slot at once") {
  auto options = host_options();
  Room host(options);
  options.host = false;
  options.server = host.local_endpoint();
  auto first = std::make_unique<Room>(options);
  Room second(options);
  double now = 0;
  pump(host, *first, now);
  pump(host, second, now);
  pump(host, *first, now);
  REQUIRE(host.participants() == 7);
  first.reset(); // the leave notice, long before the silence drop
  pump(host, second, now);
  CHECK(host.participants() == 5 && second.participants() == 5);
  CHECK(!host.recovering() && !second.recovering());
  CHECK(host.error().empty() && second.error().empty());
}

TEST_CASE("online room tells its clients when the host quits") {
  auto options = host_options();
  auto host = std::make_unique<Room>(options);
  options.host = false;
  options.server = host->local_endpoint();
  Room client(options);
  double now = 0;
  pump(*host, client, now);
  REQUIRE(client.connected());
  host.reset();
  for (int pass = 0; pass < 20; ++pass) {
    client.update(now);
    now += 0.005;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(client.error() == "The host left the online game");
}

TEST_CASE("online room drops a peer silent past the drop delay") {
  auto options = host_options();
  Room host(options);
  DatagramSocket relay(Endpoint::parse("127.0.0.1", 0));
  options.host = false;
  options.server = relay.local();
  Room client(options);
  Endpoint client_address;
  bool cut = false;
  double now = 0;
  const auto progress = [&](const double seconds) {
    for (const double end = now + seconds; now < end; now += 0.05) {
      while (auto packet = relay.receive()) {
        const bool reply = packet->sender == host.local_endpoint();
        client_address = reply ? client_address : packet->sender;
        if (!cut) {
          static_cast<void>(
              relay.send(reply ? client_address : host.local_endpoint(), packet->bytes));
        }
      }
      host.update(now);
      client.update(now);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  progress(2);
  REQUIRE(host.participants() == 3);
  cut = true;
  progress(peer_drop_seconds - 5);
  CHECK(host.recovering() && host.participants() == 3); // still waiting to resume
  progress(6);
  CHECK(host.participants() == 1 && !host.recovering() && host.error().empty());
  CHECK(client.error() == "Lost contact with the online host");
}
