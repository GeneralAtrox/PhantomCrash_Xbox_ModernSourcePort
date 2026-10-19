#include "test_support.hpp"
#include "phantomcrash/online_socket.hpp"
#include "phantomcrash/online_input.hpp"
#include <chrono>
#include <thread>

using namespace phantomcrash::online;

namespace {
std::optional<Datagram> receive_until(DatagramSocket &socket) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  do {
    if (auto packet = socket.receive()) {
      return packet;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (std::chrono::steady_clock::now() < deadline);
  return std::nullopt;
}
} // namespace

TEST_CASE("online UDP sockets exchange gameplay inputs in both directions") {
  DatagramSocket host(Endpoint::parse("127.0.0.1", 0));
  DatagramSocket client(Endpoint::parse("127.0.0.1", 0));
  CHECK(host.local().port != 0 && client.local().port != host.local().port);
  CHECK(!host.receive());
  PlayerInput input;
  input.tick = 77;
  input.player = 1;
  input.control.move[1] = 0.75F;
  CHECK(client.send(host.local(), encode_input(input)));
  const auto request = receive_until(host);
  REQUIRE(request);
  CHECK(request->sender == client.local());
  CHECK(same_input(decode_input(request->bytes), input));
  CHECK(host.send(request->sender, request->bytes));
  const auto reply = receive_until(client);
  REQUIRE(reply);
  CHECK(reply->sender == host.local());
  CHECK(same_input(decode_input(reply->bytes), input));
}

TEST_CASE("online sockets release their owned port on destruction") {
  Endpoint address;
  {
    DatagramSocket socket(Endpoint::parse("127.0.0.1", 0));
    address = socket.local();
  }
  DatagramSocket replacement(address);
  CHECK(replacement.local() == address);
}
