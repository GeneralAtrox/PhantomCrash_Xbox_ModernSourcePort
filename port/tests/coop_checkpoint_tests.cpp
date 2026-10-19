#include "test_support.hpp"
#include "rumbling_fixture.hpp"
#include "../src/desktop_coop_matches.hpp"
#include "../src/desktop_online_match.hpp"
#include "../src/desktop_online_session.hpp"
#include "phantomcrash/online_input_channel.hpp"
#include <chrono>
#include <deque>
#include <thread>
#include <tuple>

using namespace phantomcrash;
using namespace phantomcrash::rumbling_fixture;

namespace {
struct MatchFixture {
  Fixture data;
  TestDriveScript drive;
  std::map<std::string, WeaponTuning> templates;
  CoopMatch match;

  MatchFixture() {
    data.rules.roster.live_size = 5;
    const RumblingTables tables{data.parts, data.garage, data.tables, data.rules, drive, templates};
    RumblingLive live;
    live.arrival_seconds = 0.1F;
    match.world = std::make_shared<RumblingWorld>(
        data.check(), tables, BattleSettings{}, std::array<RumblingStage, roster_areas>{}, live, 7);
    match.battle = match.world->lend(0, match.simulation_save);
    REQUIRE(match.battle);
    match.battle->set_cooperative(true);
    for (int i = 0; i < 3; ++i) {
      match.players[i].actor = i;
      auto &robot = match.battle->robot(i);
      robot.piloted = true;
      robot.drone = false;
      match.battle->bring_in(i);
    }
    // Activate the two background areas too, to exercise their fractional clocks.
    for (int area = 1; area < roster_areas; ++area) {
      match.simulation_save.rosters[area] = match.simulation_save.rosters[0];
    }
    match.world->update(match.simulation_save, 0);
    match.world->prepare_deterministic();
  }

  void step(int frame) {
    if (frame % 13 == 0) {
      auto &robot = match.battle->robot(3);
      robot.alive = false;
      robot.dying = false;
      robot.shot_by = 0;
    }
    RobotControl input;
    input.move = {0.3F, 0.7F};
    match.battle->set_player_control(0, input, {});
    match.tick(static_cast<float>(tick));
  }
};

// Exact public observations, not a complete hidden-state fingerprint.
auto observe(const CoopMatch &match) {
  std::vector<float> robots;
  for (const auto &robot : match.battle->robots()) {
    const auto position = robot.mover.position();
    const auto velocity = robot.mover.velocity();
    robots.insert(robots.end(), {position.x, position.y, position.z, velocity.x, velocity.y,
                                 velocity.z, robot.mover.heading(), robot.hp, robot.shield});
  }
  std::array<int, roster_areas> changes;
  for (int area = 0; area < roster_areas; ++area) {
    changes[area] = match.world->changes(area);
  }
  return std::tuple{robots, changes, match.elapsed, match.world->bounty_won(),
                    json::write(quest_to_json(match.simulation_save))};
}

std::shared_ptr<BattleScene> rider(Battle &battle) {
  BattleSceneTables tables;
  SceneTrack track;
  track.frames = 540;
  for (int state = scene_state::entrance_first; state <= scene_state::entrance_last; ++state) {
    tables.states[state] = track;
  }
  tables.gates[6] = {{{0, 0, 40}, 0}};
  SceneTuning tuning;
  tuning.computer_entrance = false;
  tuning.computer_exit = false;
  auto scene = std::make_shared<BattleScene>(tables, tuning, 31);
  CHECK(scene->start_entrance(battle));
  return scene;
}
} // namespace

TEST_CASE("co-op rollback restores replacement RNG and background world clocks repeatedly") {
  MatchFixture f;
  f.step(0);
  const auto battle = f.match.battle;
  const auto checkpoint = f.match.checkpoint();
  const auto initial = observe(f.match);
  const int changes = f.match.world->changes(0);
  std::vector<decltype(observe(f.match))> expected;
  for (int frame = 1; frame <= 90; ++frame) {
    f.step(frame);
    expected.push_back(observe(f.match));
  }
  CHECK(f.match.world->changes(0) > changes);
  for (int repeat = 0; repeat < 2; ++repeat) {
    f.match.restore_checkpoint(*checkpoint);
    CHECK(f.match.battle == battle);
    CHECK(observe(f.match) == initial);
    for (int frame = 1; frame <= 90; ++frame) {
      f.step(frame);
      CHECK(observe(f.match) == expected[frame - 1]);
    }
  }
}

TEST_CASE("co-op rollback restores rewards participants options and scene identity") {
  MatchFixture f;
  auto &match = f.match;
  match.battle->select_player(0);
  match.rider_scenes[0] = rider(*match.battle);
  const auto scene = match.rider_scenes[0];
  match.entrances = std::make_unique<BattleScene>(*scene);
  const auto playback = scene->playback();
  REQUIRE(playback);
  const auto snapshot = match.checkpoint();
  scene->prepare(*match.battle, 0.5F);
  CHECK(scene->playback()->frame != playback->frame);
  match.players[0].bounty = 900;
  match.players[0].won = true;
  match.players[0].beaten.push_back(14);
  match.players[1].actor = -1;
  match.players[2].look.body = 123;
  match.options.share_cash = !match.options.share_cash;
  const auto changed_options = match.options;
  match.simulation_save.money = 123;
  match.look_revision = 4;
  match.day = 8;
  match.elapsed = 2;
  match.entrances.reset();
  match.rider_scenes[0].reset();
  match.restore_checkpoint(*snapshot);
  CHECK(match.rider_scenes[0] == scene);
  CHECK(scene->playback()->frame == playback->frame);
  REQUIRE(match.entrances);
  CHECK(match.entrances->playback()->frame == playback->frame);
  CHECK(match.players[0].bounty == 0);
  CHECK(!match.players[0].won);
  CHECK(match.players[0].beaten.empty());
  CHECK(match.players[1].actor == 1);
  CHECK(match.players[2].look.body != 123);
  CHECK(match.options != changed_options);
  CHECK(match.simulation_save.money != 123);
  CHECK(match.look_revision == 0);
  CHECK(match.day == 0);
  CHECK(match.elapsed == 0);
}

TEST_CASE("world checkpoint restores a rebuilt area into its retained battle") {
  MatchFixture f;
  const auto world_state = f.match.world->checkpoint();
  const auto save = f.match.simulation_save;
  const auto battle = f.match.battle;
  const auto initial = observe(f.match);
  static_cast<void>(f.match.world->give_back(f.match.simulation_save));
  ++f.match.simulation_save.day;
  f.match.world->update(f.match.simulation_save, 0.2);
  f.match.world->restore_checkpoint(*world_state);
  f.match.simulation_save = save;
  CHECK(f.match.world->lent() == 0);
  CHECK(observe(f.match) == initial);
  CHECK(f.match.world->lend(0, f.match.simulation_save) == battle);
}

TEST_CASE("checkpoints reject a different world or match") {
  MatchFixture first, second;
  const auto world_state = first.match.world->checkpoint();
  const auto match_state = first.match.checkpoint();
  CHECK_THROWS(second.match.world->restore_checkpoint(*world_state));
  CHECK_THROWS(second.match.restore_checkpoint(*match_state));
}

TEST_CASE("three networked co-op matches correct delayed inputs including entrance and world") {
  namespace net = phantomcrash::online;
  net::RoomOptions options;
  options.host = true;
  options.bind = net::Endpoint::parse("127.0.0.1", 0);
  net::Room host(options);
  options.host = false;
  options.server = host.local_endpoint();
  net::Room first(options), second(options);
  const std::array<net::Room *, 3> rooms{&host, &first, &second};
  double now = 0;
  for (int i = 0; i < 60; ++i) {
    for (auto *room : rooms) {
      room->update(now);
    }
    now += 0.005;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  for (int slot = 0; slot < 3; ++slot) {
    CHECK(rooms[slot]->local_player() == slot);
    CHECK(rooms[slot]->participants() == 7);
  }
  std::array<MatchFixture, 4> fixtures;
  std::array<std::unique_ptr<net::MatchSimulation>, 4> simulations;
  std::array<std::unique_ptr<net::Prediction>, 4> predictions;
  std::array<std::unique_ptr<net::InputChannel>, 3> channels;
  for (int slot = 0; slot < 4; ++slot) {
    auto &match = fixtures[slot].match;
    match.battle->select_player(2);
    match.rider_scenes[2] = rider(*match.battle);
    match.battle->robot(3).alive = false;
    match.battle->robot(3).dying = false;
    simulations[slot] = std::make_unique<net::MatchSimulation>(match, 7);
    predictions[slot] = std::make_unique<net::Prediction>(*simulations[slot], 7, 0, 8, 12);
    if (slot < 3) {
      channels[slot] = std::make_unique<net::InputChannel>(*rooms[slot], *predictions[slot], 42, 7);
    }
  }
  const auto input = [](std::uint32_t tick, std::uint8_t player) {
    net::PlayerInput value;
    value.tick = tick;
    value.player = player;
    value.control.move = {tick < 40 ? 0.3F : -0.3F, player == 1 ? 0.7F : -0.7F};
    value.control.jump = tick >= 20 && tick < 30;
    value.fire.camouflage = tick == 45;
    return value;
  };
  struct Delayed {
    int due{};
    net::RoomMessage message;
  };
  std::array<std::deque<Delayed>, 3> delayed;
  const auto pump = [&](int frame) {
    for (int pass = 0; pass < 3; ++pass) {
      for (int slot = 0; slot < 3; ++slot) {
        rooms[slot]->update(now);
        CHECK(rooms[slot]->error().empty());
        for (auto &message : rooms[slot]->take_messages()) {
          delayed[slot].push_back({frame + (slot == 0 ? 2 : 3), std::move(message)});
        }
        while (!delayed[slot].empty() && delayed[slot].front().due <= frame) {
          CHECK(channels[slot]->receive(delayed[slot].front().message, now));
          delayed[slot].pop_front();
        }
      }
      now += 0.001;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  const auto changes = fixtures[3].match.world->changes(0);
  for (int frame = 0; frame < 90; ++frame) {
    for (std::uint8_t slot = 0; slot < 3; ++slot) {
      const auto controls = input(frame, slot);
      channels[slot]->submit(controls, now);
      // Retrying a stalled tick must not send a different input or an extra packet.
      channels[slot]->submit(controls, now);
      predictions[3]->submit(controls);
    }
    pump(frame);
    for (auto &prediction : predictions) {
      CHECK(prediction->advance());
    }
  }
  for (int frame = 90; frame < 105; ++frame) {
    pump(frame);
  }
  CHECK(fixtures[3].match.world->changes(0) > changes);
  for (int slot = 0; slot < 3; ++slot) {
    predictions[slot]->reconcile();
    CHECK(predictions[slot]->confirmed_tick() == 90);
    CHECK(predictions[slot]->rollback_count() > 0);
    CHECK(observe(fixtures[slot].match) == observe(fixtures[3].match));
    CHECK(fixtures[slot].match.rider_scenes[2]->playback()->frame ==
          fixtures[3].match.rider_scenes[2]->playback()->frame);
  }
  net::InputFrame invalid;
  invalid.tick = 91;
  invalid.players = 7;
  CHECK_THROWS(simulations[0]->step(invalid));
  CHECK_THROWS(channels[0]->submit(input(90, 1), now));
  auto authored = input(90, 1);
  channels[1]->submit(authored, now);
  authored.control.jump = !authored.control.jump;
  CHECK_THROWS(channels[1]->submit(authored, now));
  std::vector<net::RoomMessage> authored_messages;
  for (int attempt = 0; attempt < 20 && authored_messages.empty(); ++attempt) {
    host.update(now);
    authored_messages = host.take_messages();
    now += 0.001;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(authored_messages.size() == 1);
  auto forged = authored_messages.front();
  forged.sender = 2;
  CHECK_THROWS(channels[0]->receive(forged, now));
  net::InputChannel another_match(host, *predictions[0], 99, 7);
  CHECK(!another_match.receive(authored_messages.front(), now));
  CHECK(predictions[0]->confirmed_tick() == 90);
}

TEST_CASE("desktop match session stalls without resampling and reconciles delayed controls") {
  namespace net = phantomcrash::online;
  net::RoomOptions options;
  options.host = true;
  options.bind = net::Endpoint::parse("127.0.0.1", 0);
  net::Room host(options);
  options.host = false;
  options.server = host.local_endpoint();
  net::Room client(options);
  double now = 0;
  for (int pass = 0; pass < 40; ++pass) {
    host.update(now);
    client.update(now);
    now += 0.005;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  MatchFixture first, second;
  for (auto *fixture : {&first, &second}) {
    fixture->match.players[2].actor = -1;
    fixture->match.battle->robot(2).piloted = false;
  }
  // No input delay: this follows each tick's controls exactly.
  net::MatchSession local(host, first.match, 93, 3, nullptr, 0, 0);
  net::MatchSession remote(client, second.match, 93, 3, nullptr, 0, 0);
  const auto pump = [&] {
    for (int pass = 0; pass < 8; ++pass) {
      host.update(now);
      client.update(now);
      for (const auto &message : host.take_messages()) {
        CHECK(local.receive(message, now));
      }
      for (const auto &message : client.take_messages()) {
        CHECK(remote.receive(message, now));
      }
      local.reconcile();
      remote.reconcile();
      now += 0.001;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  RobotControl control;
  control.move = {0.12345679F, 0.7F};
  control.mouse_aim = {0.0012345679F, 0};
  for (int tick = 0; tick < 8; ++tick) {
    CHECK(local.needs_input());
    local.capture(control, {}, std::nullopt, now);
    CHECK(local.advance());
    pump();
  }
  local.capture(control, {}, std::nullopt, now);
  CHECK(!local.advance());
  CHECK(!local.needs_input());
  const auto waiting = *local.local_input();
  CHECK_THROWS(local.capture({}, {}, std::nullopt, now));
  CHECK(net::same_input(waiting, *local.local_input()));
  pump();
  RobotControl other;
  other.move = {-0.4F, 0.5F};
  for (int tick = 0; tick < 9; ++tick) {
    remote.capture(other, {}, std::nullopt, now);
    CHECK(remote.advance());
    pump();
  }
  CHECK(local.advance());
  CHECK(local.next_tick() == 9);
  CHECK(local.confirmed_tick() == 9);
  CHECK(remote.confirmed_tick() == 9);
  CHECK(local.rollback_count() > 0);
  CHECK(observe(first.match) == observe(second.match));
  const auto first_digest = local.confirmed_digest(9);
  const auto second_digest = remote.confirmed_digest(9);
  CHECK(first_digest.has_value() && first_digest == second_digest);
  const auto current = net::state_digest(first.match);
  CHECK(local.confirmed_digest(5) == remote.confirmed_digest(5));
  CHECK(net::state_digest(first.match) == current);
  CHECK(!local.confirmed_digest(10).has_value());
}
