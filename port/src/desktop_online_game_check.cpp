#include "desktop_online_factory.hpp"
#include "desktop_online_game.hpp"
#include "desktop_online_session.hpp"
#include "desktop_host.hpp"
#include "online/state_writer.hpp"
#include "phantomcrash/robot_combat.hpp"
#include <chrono>
#include <iostream>
#include <thread>

namespace phantomcrash {
namespace {
void check(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(std::string("Online admission check: ") + message);
  }
}
template <class T> auto digest(const T &value) {
  online::StateWriter out;
  out(value);
  return out.finish();
}
} // namespace

void check_online_game(DesktopHost &host, const DataRoots &roots,
                       const online::BattleSetup &setup) {
  std::array<std::unique_ptr<online::Room>, 3> rooms;
  std::array<std::shared_ptr<CoopMatches>, 3> matches;
  std::array<std::unique_ptr<OnlineGame>, 3> games;
  online::RoomOptions options;
  options.host = true;
  options.bind = online::Endpoint::parse("127.0.0.1", 0);
  rooms[0] = std::make_unique<online::Room>(options);
  options.host = false;
  options.server = rooms[0]->local_endpoint();
  rooms[1] = std::make_unique<online::Room>(options);
  rooms[2] = std::make_unique<online::Room>(options);
  double now = 0;
  for (int pass = 0; pass < 40; ++pass) {
    for (auto &room : rooms) {
      room->update(now);
    }
    now += 0.005;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  for (int peer = 0; peer < 3; ++peer) {
    check(rooms[peer]->connected() && rooms[peer]->local_player() == peer &&
              rooms[peer]->participants() == 7,
          "Three-peer room did not establish");
    matches[peer] = std::make_shared<CoopMatches>();
    // No input delay: the checks below name the exact ticks each step aligns at.
    games[peer] = std::make_unique<OnlineGame>(*rooms[peer], host.renderer(), roots, matches[peer],
                                               setup.options, setup.robots, 0);
  }
  const OnlineGame::Key key{setup.world.day, setup.area, setup.contest};
  std::weak_ptr<online::MatchSession> probed;
  int commit_probes = 0;
  const auto pump = [&] {
    for (int peer = 0; peer < 3; ++peer) {
      if (!rooms[peer]) {
        continue; // this computer quit
      }
      rooms[peer]->update(now);
      check(rooms[peer]->error().empty(), "Room failed while ordering admission");
      for (const auto &message : rooms[peer]->take_messages()) {
        static_cast<void>(games[peer]->receive(message, now));
      }
      games[peer]->update(now);
      if (peer == 0 && games[0]->ready(key, 0)) {
        const auto session = games[0]->match(key)->online;
        if (probed.lock() != session) {
          // Queue the first input immediately behind the commit, before the
          // other peers process either packet. It must survive reconstruction.
          session->capture({}, {}, std::nullopt, now);
          probed = session;
          ++commit_probes;
        }
      }
    }
    now += 0.005;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  };
  const auto run_to = [&](const std::uint32_t target, const int players) {
    for (int pass = 0; pass < 2000; ++pass) {
      pump();
      bool all = true;
      for (int peer = 0; peer < 3; ++peer) {
        auto &session = *games[peer]->match(key)->online;
        if (session.next_tick() < target) {
          if (peer < players && session.needs_input()) {
            RobotControl control;
            control.move = {0.15F * (peer + 1), peer == 1 ? -0.4F : 0.6F};
            control.jump = session.next_tick() % 60 == 25;
            FireControl fire;
            fire.fire[0] = session.next_tick() % 50 < 20;
            session.capture(control, fire, std::nullopt, now);
          }
          games[peer]->advance();
        }
        all &= session.next_tick() == target && session.confirmed_tick() >= target;
      }
      if (all) {
        const auto expected = games[0]->match(key)->online->confirmed_digest(target);
        check(expected.has_value(), "Confirmed state could not be inspected");
        for (int peer = 1; peer < 3; ++peer) {
          check(games[peer]->match(key)->online->confirmed_digest(target) == expected,
                "Confirmed shared arena states diverged");
        }
        return;
      }
    }
    throw std::runtime_error("Online admission check: gameplay did not reach its target tick");
  };
  for (int entrant = 0; entrant < 3; ++entrant) {
    std::optional<online::StateDigest> original_player, original_armory;
    double elapsed = 0;
    if (entrant > 0) {
      const auto match = games[0]->match(key);
      original_player = digest(match->battle->robot(match->players[0].actor));
      original_armory = digest(match->battle->armory());
      elapsed = match->elapsed;
    }
    const auto &player = *setup.players[entrant];
    games[entrant]->request_entry(player.profile, player.entry);
    bool ready = false;
    for (int pass = 0; pass < 2000 && !ready; ++pass) {
      pump();
      ready = true;
      for (int peer = 0; peer < 3; ++peer) {
        ready &= games[peer]->ready(key, entrant);
      }
    }
    check(ready, "Arena admission never committed");
    for (int peer = 0; peer < 3; ++peer) {
      const auto match = games[peer]->match(key);
      check(match->elapsed == elapsed, "Admission advanced or reset the frozen match clock");
      if (original_player) {
        check(digest(match->battle->robot(match->players[0].actor)) == *original_player,
              "Admitting another player changed the existing player's state");
        check(digest(match->battle->armory()) == *original_armory,
              "Admitting another player reset in-flight weapon state");
      }
      for (int slot = entrant + 1; slot < 3; ++slot) {
        check(match->players[slot].actor < 0, "A player in a menu was inserted into combat");
      }
    }
    run_to(static_cast<std::uint32_t>((entrant + 1) * 60), entrant + 1);
  }
  check(commit_probes == 3, "Commit/input transport coalescing was not exercised");
  // Freeze with one already-captured, unstepped input and another peer ahead
  // in prediction. Alignment must preserve that input and confirm one state.
  auto &ahead = *games[0]->match(key)->online;
  RobotControl edge;
  edge.jump = true;
  edge.mouse_aim = {0.01F, 0.02F};
  ahead.capture(edge, {}, std::nullopt, now);
  for (int step = 0; step < 2; ++step) {
    games[2]->match(key)->online->capture({}, {}, std::nullopt, now);
    games[2]->advance();
  }
  for (int peer = 0; peer < 3; ++peer) {
    games[peer]->match(key)->online->freeze();
  }
  check(ahead.frozen_horizon() == 181, "Captured input was absent from the freeze horizon");
  check(games[2]->match(key)->online->frozen_horizon() == 182,
        "The other peer did not advance its prediction horizon");
  bool aligned = false;
  for (int pass = 0; pass < 1000 && !aligned; ++pass) {
    pump();
    aligned = true;
    for (int peer = 0; peer < 3; ++peer) {
      aligned &= games[peer]->match(key)->online->align(182, now);
    }
  }
  check(aligned, "Unequal frozen horizons did not align");
  const auto expected = ahead.confirmed_digest(182);
  for (int peer = 1; peer < 3; ++peer) {
    check(games[peer]->match(key)->online->confirmed_digest(182) == expected,
          "Alignment diverged while preserving captured input");
  }
  std::array<std::shared_ptr<CoopMatch>, 3> retained;
  for (int peer = 0; peer < 3; ++peer) {
    retained[peer] = games[peer]->match(key);
    retained[peer]->players[0].bounty = 1234;
    retained[peer]->players[0].beaten = {14, 15};
  }
  auto leaving_view = make_online_battle_view(retained[0], 0, {});
  const auto wait_departure = [&](const int slot) {
    for (int pass = 0; pass < 2000; ++pass) {
      pump();
      bool all = true;
      for (int peer = 0; peer < 3; ++peer) {
        const auto &match = retained[peer];
        all &= !games[peer] || (match->settlements[slot] && match->settlements[slot]->ready);
      }
      if (all) {
        const auto reference = digest(retained[0]->settlements[slot]);
        for (int peer = 0; peer < 3; ++peer) {
          if (games[peer]) {
            const auto &match = retained[peer];
            check(match->players[slot].actor < 0, "Committed departure retained its player");
            check(digest(match->settlements[slot]) == reference, "Committed results differ");
          }
        }
        return;
      }
    }
    throw std::runtime_error("Online departure did not commit");
  };
  games[0]->request_exit(key);
  games[0]->request_exit(key);
  wait_departure(0);
  leaving_view->tick({}, online::tick_seconds);
  check(leaving_view->done(), "View did not transition after committed exit");
  check(leaving_view->match_summary().outcome == MatchOutcome::leave,
        "Manual exit produced a different outcome");
  QuestSave receipt_profile;
  const auto before_receipt = online::state_digest(*retained[0]);
  check(leaving_view->settle_coop(receipt_profile) == 1234, "Committed bounty was lost");
  check(leaving_view->settle_coop(receipt_profile) == 0, "Committed bounty settled twice");
  check(receipt_profile.beaten == std::vector<int>({14, 15}), "Confirmed beaten records were lost");
  check(leaving_view->start_backdrop(leaving_view->stand_up_backdrop(nullptr, receipt_profile)),
        "Online result backdrop did not initialize");
  leaving_view->tick_backdrop(online::tick_seconds);
  check(online::state_digest(*retained[0]) == before_receipt,
        "Local receipt/backdrop changed the remaining players' simulation");
  run_to(242, 3);
  for (auto &match : retained) {
    auto &robot = match->battle->robot(match->players[2].actor);
    robot.alive = robot.dying = false;
  }
  run_to(243, 3);
  wait_departure(2);
  check(retained[0]->settlements[2]->summary.outcome == MatchOutcome::retire,
        "Confirmed death did not retire its player");
  run_to(303, 3);
  // The last player's computer quits mid-match without an exit. The host
  // stands in with idle controls until its departure commits.
  games[1].reset();
  rooms[1].reset();
  wait_departure(1);
  check(retained[0]->settlements[1]->summary.outcome == MatchOutcome::leave,
        "A quitting computer's player did not leave");
  check(rooms[0]->participants() == 5 && rooms[2]->participants() == 5,
        "The quitting computer kept its room slot");
  for (int peer = 0; peer < 3; peer += 2) {
    check(matches[peer]->matches.empty(), "Last departure did not retire the arena");
    check(retained[peer]->world->lent() < 0, "Last departure did not return the roster");
  }
  std::cout << "PASS: three real UDP peers commit an arena and two later admissions; "
               "menu spectators, preserved actors/projectiles, coalesced commit/input, unequal "
               "freeze horizons and 182 confirmed ticks\n";
  std::cout << "PASS: committed manual exit, once-only local settlement, isolated result backdrop, "
               "confirmed death, continued teammate gameplay, a quitting "
               "computer's stand-in departure and last-player arena retirement\n";
}
} // namespace phantomcrash
