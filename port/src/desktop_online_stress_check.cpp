#include "desktop_host.hpp"
#include "desktop_tools.hpp"
#include "desktop_online_factory.hpp"
#include "desktop_online_game.hpp"
#include "desktop_online_session.hpp"
#include "phantomcrash/canvas.hpp"
#include "phantomcrash/online_room.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <format>
#include <iostream>
#include <stdexcept>
#include <thread>

// Hidden online frame-time check: two peers on this computer play one RUMBLING
// match while its wrecks pile up. Each frame's simulation and drawing work is
// timed per peer against the 144 fps frame cap.

namespace phantomcrash {
namespace {
constexpr int peers = 2;
constexpr double frame_seconds = 1.0 / 144.0; // the frame cap's top
constexpr double latency_seconds = 0.06;      // one way, so predictions roll back
constexpr double window_seconds = 15.0;       // one report line each
constexpr double warmup_seconds = 15.0;       // loading and the entrance
constexpr double allowed_growth = 1.5;        // last window's mean against the first
constexpr std::pair<int, int> canvas_size{1280, 720};
constexpr double aim_sway = 0.5;          // right stick (aim pitch), of full deflection
constexpr long long walk_out_ticks = 600; // the entrance, then a few seconds ahead

void check(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(std::string("Online stress check: ") + message);
  }
}

struct Delayed {
  double due{};
  online::RoomMessage message;
};

struct Window {
  double mean{}, p99{}, worst{};
};

Window summarize(std::vector<double> times) {
  Window out;
  if (times.empty()) {
    return out;
  }
  std::ranges::sort(times);
  for (const double time : times) {
    out.mean += time;
  }
  out.mean /= static_cast<double>(times.size());
  out.p99 = times[static_cast<std::size_t>(0.99 * static_cast<double>(times.size() - 1))];
  out.worst = times.back();
  return out;
}

// Each pilot walks out of its gate, then stands in the open arena, as walking
// back into a gate would end its match, and fires both hands so the match
// keeps killing. Its aim sways up and down every tick, so every remote input
// mispredicts and rolls back, as a real pilot's moving stick does.
InputFrame pilot(const int peer, const long long tick) {
  InputFrame input;
  const double phase = static_cast<double>(tick) * 0.05 + peer;
  input.right_stick = {0.0F, static_cast<float>(aim_sway * std::sin(phase))};
  if (tick < walk_out_ticks) {
    input.left_stick = {0.0F, 1.0F}; // out of its gate, into the open arena
  }
  input.held = button::right_trigger | ((tick / 90) % 2 ? button::left_trigger : 0U);
  input.ports[0].connected = true;
  input.ports[0].held = input.held;
  input.ports[0].left_stick = input.left_stick;
  input.ports[0].right_stick = input.right_stick;
  return input;
}
} // namespace

int check_online_stress(const DataRoots &roots, const double game_seconds) {
  DesktopHost host(false, true, canvas_size);
  auto resources = std::make_shared<OnlineBattleResources>(host.renderer(), roots);
  check(!resources->rumbling.quick_rentals.empty(), "No quick rental to enter");
  const auto &preset = resources->rumbling.quick_rentals.front();
  online::BattleSetup setup;
  for (int slot = 0; slot < peers; ++slot) {
    online::BattlePlayer player;
    player.profile.player_name = "Stress player " + std::to_string(slot + 1);
    player.profile.money = 1000000;
    const auto entry = prepare_quick_rental(preset, resources->contests, resources->rules,
                                            *resources->garage, player.profile);
    check(entry.has_value(), "Rental entry failed");
    player.entry = *entry;
    setup.players[slot] = std::move(player);
  }
  setup.world = setup.players[0]->profile;
  setup.area = setup.players[0]->entry.area;
  setup.contest = setup.players[0]->entry.contest;
  resources.reset();

  std::array<std::unique_ptr<online::Room>, peers> rooms;
  std::array<std::shared_ptr<CoopMatches>, peers> matches;
  std::array<std::unique_ptr<OnlineGame>, peers> games;
  std::array<std::deque<Delayed>, peers> inboxes;
  online::RoomOptions room_options;
  room_options.host = true;
  room_options.bind = online::Endpoint::parse("127.0.0.1", 0);
  rooms[0] = std::make_unique<online::Room>(room_options);
  room_options.host = false;
  room_options.server = rooms[0]->local_endpoint();
  rooms[1] = std::make_unique<online::Room>(room_options);
  double now = 0;
  for (int pass = 0; pass < 200 && !(rooms[0]->connected() && rooms[1]->connected()); ++pass) {
    for (auto &room : rooms) {
      room->update(now);
    }
    now += 0.005;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  for (int peer = 0; peer < peers; ++peer) {
    check(rooms[peer]->connected() && rooms[peer]->local_player() == peer, "Room did not connect");
    matches[peer] = std::make_shared<CoopMatches>();
    games[peer] = std::make_unique<OnlineGame>(*rooms[peer], host.renderer(), roots, matches[peer],
                                               setup.options, setup.robots);
  }
  const OnlineGame::Key key{setup.world.day, setup.area, setup.contest};

  const auto pump = [&](const int peer) {
    rooms[peer]->update(now);
    check(rooms[peer]->error().empty(), "Room failed");
    for (auto &message : rooms[peer]->take_messages()) {
      inboxes[peer].push_back({now + latency_seconds, std::move(message)});
    }
    while (!inboxes[peer].empty() && inboxes[peer].front().due <= now) {
      static_cast<void>(games[peer]->receive(inboxes[peer].front().message, now));
      inboxes[peer].pop_front();
    }
    games[peer]->update(now);
  };
  for (int entrant = 0; entrant < peers; ++entrant) {
    const auto &player = *setup.players[entrant];
    games[entrant]->request_entry(player.profile, player.entry);
    bool ready = false;
    for (int pass = 0; pass < 4000 && !ready; ++pass) {
      ready = true;
      for (int peer = 0; peer < peers; ++peer) {
        pump(peer);
        ready &= games[peer]->ready(key, entrant);
      }
      now += frame_seconds;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(ready, "Arena admission never committed");
  }
  std::array<std::unique_ptr<TrainingScreen>, peers> views;
  for (int peer = 0; peer < peers; ++peer) {
    // Before the first tick on both peers, so their states stay identical:
    // pilots that never die or run dry keep the match going and killing.
    games[peer]->match(key)->battle->set_cheats({true, true});
    views[peer] = make_online_battle_view(games[peer]->match(key), peer, {});
  }
  const auto match_of = [&](const int peer) {
    check(matches[peer]->matches.contains(key), "Every pilot left; the match ended");
    return games[peer]->match(key);
  };

  using Clock = std::chrono::steady_clock;
  std::array<std::vector<double>, peers> times, simulation_times, draw_times;
  std::array<Window, peers> first_window{}, last_window{};
  bool have_first = false;
  double clock = 0, carry = 0, next_report = warmup_seconds + window_seconds;
  long long ticks = 0;
  // As CoopScreen schedules it: receiving and rollback in frames between
  // logic ticks, a tick frame pumping only when no such frame came first.
  std::array<bool, peers> pumped{};
  const auto canvas = fit_canvas(canvas_size.first, canvas_size.second);
  while (clock < warmup_seconds + game_seconds) {
    carry += frame_seconds;
    const int due = static_cast<int>(carry / online::tick_seconds);
    carry -= due * online::tick_seconds;
    for (int peer = 0; peer < peers; ++peer) {
      const auto start = Clock::now();
      if (due == 0) {
        pump(peer);
        pumped[peer] = true;
      }
      for (int tick = 0; tick < due; ++tick) {
        if (!pumped[peer]) {
          pump(peer);
        }
        pumped[peer] = false;
        if (views[peer] && !views[peer]->done()) {
          views[peer]->tick(pilot(peer, ticks + tick), online::tick_seconds);
        } else if (const auto session = match_of(peer)->online; session && session->needs_input()) {
          // A pilot shot down keeps sending neutral input, as its screen would.
          views[peer].reset();
          session->capture({}, {}, std::nullopt, now);
        }
        games[peer]->advance();
      }
      const auto simulated = Clock::now();
      if (views[peer]) {
        views[peer]->draw(canvas);
      }
      if (clock >= warmup_seconds) {
        const auto drawn = Clock::now();
        using Milliseconds = std::chrono::duration<double, std::milli>;
        times[peer].push_back(Milliseconds(drawn - start).count());
        simulation_times[peer].push_back(Milliseconds(simulated - start).count());
        draw_times[peer].push_back(Milliseconds(drawn - simulated).count());
      }
    }
    SDL_RenderPresent(host.renderer());
    ticks += due;
    clock += frame_seconds;
    now += frame_seconds;
    if (clock >= next_report) {
      next_report += window_seconds;
      const auto match = match_of(0);
      for (int slot = 0; slot < peers; ++slot) {
        check(match->players[slot].actor >= 0,
              "A pilot left the match; its peer stopped rolling back");
      }
      // mean/p99 milliseconds of each peer's frame, simulation and drawing.
      std::string line =
          std::format("t={:5.0f}s wrecks={:3} rollbacks={:6}", clock - warmup_seconds,
                      match->battle->wrecks().items().size(), match->online->rollback_count());
      for (int peer = 0; peer < peers; ++peer) {
        const auto window = summarize(std::move(times[peer]));
        const auto simulation = summarize(std::move(simulation_times[peer]));
        const auto drawing = summarize(std::move(draw_times[peer]));
        times[peer].clear();
        simulation_times[peer].clear();
        draw_times[peer].clear();
        line += std::format(
            " | peer{} frame {:5.2f}/{:5.2f} sim {:5.2f}/{:5.2f} draw {:5.2f}/{:5.2f}", peer + 1,
            window.mean, window.p99, simulation.mean, simulation.p99, drawing.mean, drawing.p99);
        (have_first ? last_window : first_window)[peer] = window;
      }
      have_first = true;
      std::cout << line << '\n' << std::flush;
    }
  }
  const double budget = 1000.0 * frame_seconds;
  bool pass = true;
  for (int peer = 0; peer < peers; ++peer) {
    const bool fast = last_window[peer].p99 <= budget;
    const bool flat = last_window[peer].mean <= first_window[peer].mean * allowed_growth;
    pass &= fast && flat;
    std::cout << std::format("peer{}: last p99 {:.2f} ms of {:.2f} ms budget ({}), mean {:.2f} -> "
                             "{:.2f} ms ({})\n",
                             peer + 1, last_window[peer].p99, budget, fast ? "ok" : "OVER",
                             first_window[peer].mean, last_window[peer].mean,
                             flat ? "flat" : "GROWING");
  }
  std::cout << (pass ? "PASS" : "FAIL") << ": online stress, " << game_seconds
            << " s of play at 144 fps\n";
  return pass ? 0 : 1;
}

} // namespace phantomcrash
