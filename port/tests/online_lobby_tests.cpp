#include "test_support.hpp"
#include "phantomcrash/online_lobby.hpp"
#include <chrono>
#include <thread>

using namespace phantomcrash::online;

TEST_CASE("online lobby relays UI presence and initializes a later peer") {
  RoomOptions options;
  options.host = true;
  options.bind = Endpoint::parse("127.0.0.1", 0);
  Lobby host(options);
  options.host = false;
  options.server = host.room().local_endpoint();
  Lobby first(options);
  double now = 0;
  host.set_presence({"Host", "garage", "garage"});
  first.set_presence({"First", "town_map", "town_map"});
  const auto pump = [&](Lobby *later) {
    for (int pass = 0; pass < 40; ++pass) {
      host.update(now);
      first.update(now);
      if (later) {
        later->update(now);
      }
      now += 0.005;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  pump(nullptr);
  CHECK(host.presence()[1].screen == "town_map");
  CHECK(first.presence()[0].screen == "garage");
  Lobby later(options);
  later.set_presence({"Later", "lobby", "lobby"});
  pump(&later);
  CHECK(later.room().local_player() == 2);
  CHECK(later.presence()[0].name == "Host");
  CHECK(later.presence()[1].name == "First");
  CHECK(first.presence()[2].name == "Later");
  first.set_presence({"First", "module_shop", "wild_machines"});
  pump(&later);
  CHECK(host.presence()[1].screen == "module_shop");
  CHECK(later.presence()[1].location == "wild_machines");
  // Menu marks place the peer's rider icon in each player's own menus.
  Presence shopping{"First", "module_shop", "wild_machines", 43, {}};
  shopping.marks.push_back({"module_shop", "arm", "wild_machines", "", {120.5F, 88.0F}, false});
  shopping.marks.push_back({"town_map", "wild_machines", "map", "", {300.0F, 140.0F}, true});
  first.set_presence(shopping);
  pump(&later);
  CHECK(host.presence()[1] == shopping);
  CHECK(later.presence()[1] == shopping);
  Presence crowded = shopping;
  crowded.marks.resize(max_menu_marks + 1);
  CHECK_THROWS(first.set_presence(crowded));
  const std::array<std::uint8_t, 4> application{42, 3, 2, 1};
  first.room().send(0, application, now);
  pump(&later);
  const auto messages = host.take_messages();
  REQUIRE(messages.size() == 1);
  CHECK(messages.front().sender == 1);
  CHECK(messages.front().bytes == (std::vector<std::uint8_t>{42, 3, 2, 1}));
  CHECK_THROWS(host.set_presence({std::string(129, 'a'), "garage", "garage"}));
}

TEST_CASE("online lobby retains latest menu presence while a different peer is disconnected") {
  RoomOptions options;
  options.host = true;
  options.bind = Endpoint::parse("127.0.0.1", 0);
  Lobby host(options);
  DatagramSocket proxy(Endpoint::parse("127.0.0.1", 0));
  options.host = false;
  options.server = proxy.local();
  Lobby first(options);
  options.server = host.room().local_endpoint();
  std::unique_ptr<Lobby> second;
  Endpoint client;
  double now = 0;
  const auto pump = [&](const int count, const bool drop) {
    for (int pass = 0; pass < count; ++pass) {
      while (auto packet = proxy.receive()) {
        const bool reply = packet->sender == host.room().local_endpoint();
        if (!reply) {
          client = packet->sender;
        }
        if (!drop && (!reply || client.port)) {
          static_cast<void>(
              proxy.send(reply ? client : host.room().local_endpoint(), packet->bytes));
        }
      }
      host.update(now);
      first.update(now);
      if (second) {
        second->set_presence({"Second", "menu-" + std::to_string(pass), "town"});
        second->update(now);
      }
      now += 0.05;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  pump(40, false);
  REQUIRE(first.room().local_player() == 1);
  second = std::make_unique<Lobby>(options);
  pump(40, false);
  REQUIRE(second->room().local_player() == 2);
  pump(280, true);
  CHECK(host.room().recovering());
  CHECK(host.room().error().empty());
  pump(100, false);
  // Stop producing changes and allow the final retained presence to propagate.
  for (int pass = 0; pass < 80; ++pass) {
    while (auto packet = proxy.receive()) {
      static_cast<void>(proxy.send(
          packet->sender == host.room().local_endpoint() ? client : host.room().local_endpoint(),
          packet->bytes));
    }
    host.update(now);
    first.update(now);
    second->update(now);
    now += 0.05;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(!host.room().recovering() && !first.room().recovering());
  CHECK(host.room().error().empty() && first.room().error().empty() &&
        second->room().error().empty());
  CHECK(first.presence()[2].screen == "menu-99");
  CHECK(host.presence()[2] == first.presence()[2]);
}
