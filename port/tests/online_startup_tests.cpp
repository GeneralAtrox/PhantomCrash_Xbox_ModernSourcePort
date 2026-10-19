#include "test_support.hpp"
#include "phantomcrash/online_agreement.hpp"
#include "phantomcrash/online_battle_setup.hpp"
#include "../src/online/wire.hpp"
#include <chrono>
#include <bit>
#include <thread>

using namespace phantomcrash::online;

namespace {
struct Network {
  std::array<std::unique_ptr<Room>, 3> rooms;
  std::array<std::unique_ptr<RecordChannel>, 3> channels;
  std::array<std::unique_ptr<TimelineAgreement>, 3> agreements;
  double now{};
  int unhandled{};
  std::array<std::vector<Record>, 3> received;
  Network() {
    RoomOptions options;
    options.host = true;
    options.bind = Endpoint::parse("127.0.0.1", 0);
    rooms[0] = std::make_unique<Room>(options);
    options.host = false;
    options.server = rooms[0]->local_endpoint();
    rooms[1] = std::make_unique<Room>(options);
    rooms[2] = std::make_unique<Room>(options);
    for (int pass = 0; pass < 40; ++pass) {
      for (auto &room : rooms) {
        room->update(now);
      }
      now += 0.005;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (int peer = 0; peer < 3; ++peer) {
      REQUIRE(rooms[peer]->connected());
      CHECK(rooms[peer]->local_player() == peer);
      CHECK(rooms[peer]->participants() == 7);
      channels[peer] = std::make_unique<RecordChannel>(*rooms[peer]);
      agreements[peer] = std::make_unique<TimelineAgreement>(*rooms[peer], *channels[peer]);
    }
  }
  void pump(int count = 1) {
    for (int pass = 0; pass < count; ++pass) {
      for (int peer = 0; peer < 3; ++peer) {
        rooms[peer]->update(now);
        CHECK(rooms[peer]->error().empty());
        for (const auto &message : rooms[peer]->take_messages()) {
          if (!channels[peer]->receive(message)) {
            ++unhandled;
          }
        }
        for (auto &record : channels[peer]->take_records()) {
          if (!agreements[peer]->receive(record, now)) {
            received[peer].push_back(std::move(record));
          }
        }
        agreements[peer]->update(now);
        channels[peer]->update(now);
      }
      now += 0.005;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
};

RoomMessage fragment(std::uint64_t id, std::uint32_t total, std::uint32_t offset,
                     const std::vector<std::uint8_t> &bytes) {
  WireWriter out;
  out.u32(0x524f4350);
  out.u16(1);
  out.u16(7);
  out.u64(id);
  out.u32(total);
  out.u32(offset);
  out.u16(static_cast<std::uint16_t>(bytes.size()));
  out.raw(bytes);
  return {0, out.take()};
}
} // namespace

TEST_CASE("large online records transfer without exhausting the reliable input window") {
  Network net;
  std::vector<std::uint8_t> state(240000);
  for (std::size_t index = 0; index < state.size(); ++index) {
    state[index] = static_cast<std::uint8_t>(index * 13);
  }
  net.channels[0]->send(1, 7, state);
  net.channels[0]->send(1, 8, {});
  net.channels[1]->send(0, 9, {1, 2, 3});
  // Deliberately don't service the recipient until the window fills. The
  // transfer must wait, leaving packet slots for gameplay and heartbeats.
  for (int pass = 0; pass < 20; ++pass) {
    net.channels[0]->update(net.now);
  }
  CHECK(net.channels[0]->pending());
  CHECK(net.rooms[0]->send_capacity(1) >= 32);
  const std::array<std::uint8_t, 3> control{91, 92, 93};
  net.rooms[0]->send(1, control, net.now);
  net.pump(100);
  REQUIRE(net.received[1].size() == 2);
  CHECK(net.received[1][0].sender == 0);
  CHECK(net.received[1][0].kind == 7);
  CHECK(net.received[1][0].bytes == state);
  CHECK(net.received[1][1].kind == 8);
  CHECK(net.received[1][1].bytes.empty());
  REQUIRE(net.received[0].size() == 1);
  CHECK(net.received[0][0].sender == 1);
  CHECK(net.received[0][0].bytes == (std::vector<std::uint8_t>{1, 2, 3}));
  CHECK(net.unhandled == 1);
  CHECK(!net.channels[0]->pending());
  CHECK_THROWS(net.channels[1]->send(2, 7, {1}));
  CHECK_THROWS(net.channels[0]->send(0, 7, {1}));
}

TEST_CASE("online record assembly rejects gaps mixed identities and replayed records") {
  Network net;
  auto &receiver = *net.channels[1];
  CHECK_THROWS(receiver.receive(fragment(1, 4, 1, {1})));
  CHECK_THROWS(receiver.receive(fragment(1, max_record_bytes + 1, 0, {1})));
  CHECK(receiver.receive(fragment(1, 4, 0, {1, 2})));
  CHECK_THROWS(receiver.receive(fragment(2, 4, 2, {3, 4})));
  CHECK_THROWS(receiver.receive(fragment(1, 5, 2, {3, 4})));
  CHECK_THROWS(receiver.receive(fragment(1, 4, 3, {4})));
  CHECK(receiver.receive(fragment(1, 4, 2, {3, 4})));
  const auto result = receiver.take_records();
  REQUIRE(result.size() == 1);
  CHECK(result[0].bytes == (std::vector<std::uint8_t>{1, 2, 3, 4}));
  CHECK_THROWS(receiver.receive(fragment(1, 4, 0, {1, 2})));
}

TEST_CASE("online start waits for every initialized peer and rejects divergent state") {
  Network net;
  TimelineProposal proposal;
  proposal.timeline = 1;
  proposal.first_tick = 42;
  proposal.peers = 7;
  proposal.setup.assign(180000, 57);
  net.agreements[0]->propose(proposal, net.now);
  CHECK_THROWS(net.agreements[1]->propose(proposal, net.now));
  net.pump(80);
  for (const auto &agreement : net.agreements) {
    CHECK(agreement->phase() == TimelineAgreement::Phase::preparing);
    CHECK(agreement->proposal().setup == proposal.setup);
    CHECK(agreement->proposal().first_tick == 42);
  }
  Compatibility digest{};
  digest[0] = 31;
  net.agreements[1]->ready(digest);
  net.pump(10);
  CHECK(net.agreements[0]->phase() == TimelineAgreement::Phase::preparing);
  net.agreements[0]->ready(digest);
  net.pump(10);
  CHECK(net.agreements[0]->phase() == TimelineAgreement::Phase::preparing);
  net.agreements[2]->ready(digest);
  net.pump(15);
  for (const auto &agreement : net.agreements) {
    CHECK(agreement->phase() == TimelineAgreement::Phase::committed);
  }
  proposal.previous = 1;
  proposal.timeline = 2;
  proposal.first_tick = 73;
  net.agreements[0]->propose(proposal, net.now);
  net.pump(80);
  net.agreements[0]->ready(digest);
  digest[9] = 99;
  net.agreements[1]->ready(digest);
  net.pump(15);
  for (const auto &agreement : net.agreements) {
    CHECK(agreement->phase() == TimelineAgreement::Phase::failed);
    CHECK(agreement->error() == "Online initial simulation states do not match");
  }
}

TEST_CASE("online preparation errors and timeouts cannot leave a waiting peer running") {
  Network net;
  TimelineProposal proposal{1, 0, 0, 7, {3, 4, 5}};
  net.agreements[0]->propose(proposal, net.now);
  net.pump(15);
  net.agreements[2]->reject("Required robot model failed to load");
  net.pump(15);
  for (const auto &agreement : net.agreements) {
    CHECK(agreement->phase() == TimelineAgreement::Phase::failed);
    CHECK(agreement->error() == "Required robot model failed to load");
  }
  Network timeout;
  timeout.agreements[0]->propose(proposal, timeout.now);
  timeout.pump(10);
  timeout.agreements[0]->update(timeout.now + 61);
  timeout.pump(15);
  for (const auto &agreement : timeout.agreements) {
    CHECK(agreement->phase() == TimelineAgreement::Phase::failed);
    CHECK(agreement->error() == "Online battle preparation timed out");
  }
}

TEST_CASE("online battle setup preserves independent profiles roster values and assigned slots") {
  BattleSetup setup;
  setup.seed = 0xffffffffU;
  setup.area = 1;
  setup.contest = 12;
  setup.world.day = 4;
  setup.world.live_epoch = 789; // process-local world identity is not a genesis field
  setup.world.money = 20000;
  setup.world.player_name = "First entrant";
  auto &roster = setup.world.rosters[1];
  roster.day = 4;
  roster.contest = 12;
  phantomcrash::RosterMember member;
  member.entrant = 3;
  member.live.placed = true;
  member.live.position = {0.12345679F, -123.98765F, 9999.333F};
  member.live.heading = -0.012345679F;
  member.live.armour = 345.6789F;
  member.live.ammo = {1, 2, 3, 4};
  roster.members.push_back(member);
  const phantomcrash::RumblingEntry entry{1, 12, 2, true, 3, -1};
  setup.players[0] = BattlePlayer{setup.world, entry};
  setup.players[2] = BattlePlayer{setup.world, entry};
  setup.players[2]->profile.player_name = "Second entrant";
  setup.players[2]->profile.money = 3456;
  setup.players[2]->entry.gate = 3;
  setup.options.enemy_level_offset = -3;
  setup.options.kill_ny_percent = 135;
  const auto bytes = encode_battle_setup(setup);
  const auto restored = decode_battle_setup(bytes);
  CHECK(restored.seed == setup.seed);
  CHECK(restored.participants() == 5);
  CHECK(!restored.players[1]);
  CHECK(restored.players[2]->profile.player_name == "Second entrant");
  CHECK(restored.players[2]->profile.money == 3456);
  CHECK(restored.players[2]->entry.gate == 3);
  CHECK(restored.options == setup.options);
  CHECK(restored.world.live_epoch == 0);
  const auto &live = restored.world.rosters[1].members[0].live;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    CHECK(std::bit_cast<std::uint32_t>(live.position[axis]) ==
          std::bit_cast<std::uint32_t>(member.live.position[axis]));
  }
  CHECK(std::bit_cast<std::uint32_t>(live.armour) ==
        std::bit_cast<std::uint32_t>(member.live.armour));
  CHECK(std::bit_cast<std::uint32_t>(live.heading) ==
        std::bit_cast<std::uint32_t>(member.live.heading));
  CHECK(live.ammo == member.live.ammo);
  CHECK(encode_battle_setup(restored) == bytes);
  setup.players[2]->profile.day = 5;
  CHECK_THROWS(encode_battle_setup(setup));
  setup.players[2]->profile.day = 4;
  setup.players[2]->entry.area = 0;
  CHECK_THROWS(encode_battle_setup(setup));
  auto unsupported = bytes;
  const std::string text(bytes.begin(), bytes.end());
  const auto version = text.find('1', text.find("\"version\""));
  REQUIRE(version != std::string::npos);
  unsupported[version] = '2';
  CHECK_THROWS(decode_battle_setup(unsupported));
}
