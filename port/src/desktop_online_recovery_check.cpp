#include "desktop_online_factory.hpp"
#include "desktop_online_game.hpp"
#include "desktop_online_session.hpp"
#include "desktop_host.hpp"
#include <chrono>
#include <iostream>
#include <thread>

namespace phantomcrash {
void check_online_recovery(DesktopHost &host, const DataRoots &roots,
                           const online::BattleSetup &setup) {
  const auto require = [](const bool pass, const char *message) {
    if (!pass) {
      throw std::runtime_error(std::string("Live joining check: ") + message);
    }
  };
  std::array<std::unique_ptr<online::Room>, 3> rooms;
  std::array<std::shared_ptr<CoopMatches>, 3> matches;
  std::array<std::unique_ptr<OnlineGame>, 3> games;
  online::RoomOptions options;
  options.host = true;
  options.bind = online::Endpoint::parse("127.0.0.1", 0);
  int connected = 0;
  const auto connect = [&] {
    const int peer = connected++;
    rooms[peer] = std::make_unique<online::Room>(options);
    matches[peer] = std::make_shared<CoopMatches>();
    // No input delay: the checks below name the exact ticks each step aligns at.
    games[peer] = std::make_unique<OnlineGame>(*rooms[peer], host.renderer(), roots, matches[peer],
                                               setup.options, setup.robots, 0);
    options.host = false;
    options.server = rooms[0]->local_endpoint();
  };
  double now = 0;
  const auto pump = [&] {
    for (int peer = 0; peer < connected; ++peer) {
      rooms[peer]->update(now);
      require(rooms[peer]->error().empty(), "Room error");
      for (const auto &message : rooms[peer]->take_messages()) {
        games[peer]->receive(message, now);
      }
      games[peer]->update(now);
    }
    now += 0.002;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  };
  const OnlineGame::Key key{setup.world.day, setup.area, setup.contest};
  const auto agree = [&](const int slot) {
    for (int pass = 0; pass < 6000; ++pass) {
      pump();
      bool all = true;
      for (int peer = 0; peer < connected; ++peer) {
        all &= games[peer]->ready(key, slot);
      }
      if (all) {
        const auto expected = online::state_digest(*games[0]->match(key));
        for (int peer = 1; peer < connected; ++peer) {
          require(online::state_digest(*games[peer]->match(key)) == expected,
                  "Full gameplay state differs after catch-up/admission");
        }
        return;
      }
    }
    throw std::runtime_error("Live joining check: agreement timed out");
  };
  const auto run = [&](const std::uint32_t tick) {
    for (int pass = 0; pass < 6000; ++pass) {
      pump();
      bool all = true;
      for (int peer = 0; peer < connected; ++peer) {
        auto &session = *games[peer]->match(key)->online;
        if (session.next_tick() < tick) {
          if (session.needs_input()) {
            RobotControl control;
            control.move = {0.2F, 0.7F};
            control.jump = session.next_tick() % 60 == 10;
            FireControl fire;
            fire.fire[0] = session.next_tick() % 60 < 30;
            session.capture(control, fire, std::nullopt, now);
          }
          games[peer]->advance();
        }
        all &= session.next_tick() == tick && session.confirmed_tick() >= tick;
      }
      if (all) {
        pump(); // Collect the final confirmed history before the next connection.
        return;
      }
    }
    throw std::runtime_error("Live joining check: gameplay timed out");
  };
  connect();
  games[0]->request_entry(setup.players[0]->profile, setup.players[0]->entry);
  agree(0);
  run(180);
  const auto before = online::state_digest(*games[0]->match(key));
  connect(); // This process did not exist during genesis or the first 180 ticks.
  agree(0);
  require(online::state_digest(*games[0]->match(key)) == before,
          "Live catch-up changed the running host arena");
  require(games[1]->match(key)->players[1].actor < 0, "Menu newcomer entered combat early");
  games[1]->request_entry(setup.players[1]->profile, setup.players[1]->entry);
  agree(1);
  run(360);
  games[1]->request_exit(key);
  for (int pass = 0; pass < 6000; ++pass) {
    pump();
    if (games[0]->match(key)->settlements[1] && games[0]->match(key)->settlements[1]->ready &&
        games[1]->match(key)->settlements[1] && games[1]->match(key)->settlements[1]->ready) {
      break;
    }
  }
  require(games[0]->match(key)->players[1].actor < 0, "Prior exit did not commit");
  run(480);
  connect(); // Replay includes the first newcomer's admission and completed exit.
  agree(0);
  require(games[2]->match(key)->settlements[1].has_value(), "Catch-up lost a prior settlement");
  games[2]->request_entry(setup.players[2]->profile, setup.players[2]->entry);
  agree(2);
  run(600);
  const auto expected = games[0]->match(key)->online->confirmed_digest(600);
  for (int peer = 1; peer < connected; ++peer) {
    require(games[peer]->match(key)->online->confirmed_digest(600) == expected,
            "Continued combat diverged after live joins");
  }
  std::cout << "PASS: fresh UDP peers join after 180 and 480 gameplay ticks; confirmed history "
               "reconstructs combat, prior admission and exit; all three agree through tick 600\n";
}
} // namespace phantomcrash
