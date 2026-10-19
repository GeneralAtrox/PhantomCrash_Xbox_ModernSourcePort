#include "test_support.hpp"
#include "phantomcrash/online_discovery.hpp"
#include "phantomcrash/online_room.hpp"
#include "../src/online/wire.hpp"
#include <chrono>
#include <thread>

using namespace phantomcrash::online;
namespace {
Endpoint loopback(std::uint16_t port = 0) {
  return Endpoint::parse("127.0.0.1", port);
}
template <class Work, class Done> void until(Work work, Done done) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!done() && std::chrono::steady_clock::now() < deadline) {
    work();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(done());
}
} // namespace

TEST_CASE("LAN discovers multiple hosts, refreshes counts and expires closed rooms") {
  LanAdvertiser first(34501, loopback()), second(34502, loopback());
  LanBrowser browser({first.local(), second.local()});
  until(
      [&] {
        browser.update(0);
        first.update("First", 1, 3);
        second.update("Second", 3, 3);
      },
      [&] { return browser.rooms().size() == 2; });
  CHECK(browser.rooms()[0].server == loopback(34501));
  CHECK(browser.rooms()[1].name == "Second");
  CHECK(browser.rooms()[1].players == browser.rooms()[1].capacity);
  until(
      [&] {
        browser.update(1);
        first.update("Renamed", 2, 3);
      },
      [&] { return browser.rooms()[0].players == 2; });
  CHECK(browser.rooms().size() == 2);
  CHECK(browser.rooms()[0].name == "Renamed");
  browser.update(4.1);
  REQUIRE(browser.rooms().size() == 1);
  CHECK(browser.rooms()[0].server == loopback(34501));
  browser.update(5.1);
  CHECK(browser.rooms().empty());
}

TEST_CASE("LAN ignores malformed unsolicited replies and sanitizes host labels") {
  DatagramSocket sender(loopback());
  LanBrowser browser({sender.local()});
  browser.update(0);
  std::optional<Datagram> query;
  until([&] { query = sender.receive(); }, [&] { return query.has_value(); });
  REQUIRE(query);
  auto wrong = query->bytes;
  wrong[5] = 2;
  CHECK(sender.send(query->sender, wrong));
  browser.update(0);
  CHECK(browser.rooms().empty());
  WireReader in(query->bytes);
  static_cast<void>(in.raw(6));
  const auto nonce = in.u64();
  const auto response = [&](std::uint64_t token, std::uint8_t players) {
    WireWriter out;
    out.u32(0x4e4c4350);
    out.u8(1);
    out.u8(2);
    out.u64(token);
    out.u64(17);
    out.u16(34501);
    out.u8(players);
    out.u8(3);
    out.u8(4);
    out.u8('T');
    out.u8('\n');
    out.u8(255);
    out.u8('X');
    return out.take();
  };
  CHECK(sender.send(query->sender, response(nonce + 1, 1)));
  CHECK(sender.send(query->sender, response(nonce, 4)));
  CHECK(sender.send(query->sender, response(nonce, 1)));
  until([&] { browser.update(0); }, [&] { return browser.rooms().size() == 1; });
  CHECK(browser.rooms()[0].name == "T??X");
  DatagramSocket other_interface(loopback());
  CHECK(other_interface.send(query->sender, response(nonce, 2)));
  until([&] { browser.update(1); }, [&] { return browser.rooms()[0].players == 2; });
  CHECK(browser.rooms().size() == 1);
}

TEST_CASE("LAN host labels keep fullwidth player names and truncate on whole characters") {
  const auto discover = [](const std::string &name) {
    LanAdvertiser advert(34503, loopback());
    LanBrowser browser({advert.local()});
    until(
        [&] {
          browser.update(0);
          advert.update(name, 1, 3);
        },
        [&] { return !browser.rooms().empty(); });
    return browser.rooms().empty() ? std::string() : browser.rooms()[0].name;
  };
  CHECK(discover("\xef\xbc\xb3\xef\xbd\x81\xef\xbd\x8d") == "\xef\xbc\xb3\xef\xbd\x81\xef\xbd\x8d");
  // Fourteen 3-byte characters: the 40-byte limit keeps thirteen whole ones.
  std::string long_name;
  for (int i = 0; i < 14; ++i) {
    long_name += "\xef\xbd\x81";
  }
  CHECK(discover(long_name) == long_name.substr(0, 39));
}

TEST_CASE("discovered endpoint joins the normal room and retains compatibility checks") {
  RoomOptions options;
  options.host = true;
  options.bind = loopback();
  Room host(options);
  LanAdvertiser advert(host.local_endpoint().port, loopback());
  LanBrowser browser({advert.local()});
  until(
      [&] {
        browser.update(0);
        advert.update("Host", 1, 3);
      },
      [&] { return !browser.rooms().empty(); });
  REQUIRE(!browser.rooms().empty());
  options.host = false;
  options.server = browser.rooms()[0].server;
  Room client(options);
  double now = 0;
  until(
      [&] {
        now += .001;
        host.update(now);
        client.update(now);
      },
      [&] { return client.connected(); });
  CHECK(client.local_player() == 1);
  options.compatibility[0] = 1;
  Room incompatible(options);
  until(
      [&] {
        now += .001;
        host.update(now);
        incompatible.update(now);
      },
      [&] { return !incompatible.error().empty(); });
  CHECK(!incompatible.connected());
}

TEST_CASE("LAN interface broadcasts and loopback yield one room per host") {
  LanAdvertiser advert(34503, {0, 0});
  LanBrowser browser(local_broadcasts(advert.local().port));
  until(
      [&] {
        browser.update(0);
        advert.update("One host", 1, 3);
      },
      [&] { return !browser.rooms().empty(); });
  until(
      [&] {
        browser.update(1);
        advert.update("One host", 2, 3);
      },
      [&] { return browser.rooms()[0].players == 2; });
  CHECK(browser.rooms().size() == 1);
  CHECK(browser.rooms()[0].server.port == 34503);
}
