// phantomcrash_ai_sim: runs the live RUMBLING's computer robots in battle
// as fast as the machine allows, with no window, renderer or audio, and
// records what their AI does (ai_sim_probe.hpp). A research tool; the game
// does not use it. Runs are spread over worker threads, each with its own
// copy of the game data.
//
//   phantomcrash_ai_sim --out research-output/ai-sim --runs 8 --seconds 300

#include "ai_sim_navdump.hpp"
#include "ai_sim_traps.hpp"
#include "ai_sim_output.hpp"
#include "ai_sim_probe.hpp"
#include "ai_sim_watch.hpp"
#include "ai_sim_world.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace phantomcrash;
using namespace phantomcrash::ai_sim;
using Clock = std::chrono::steady_clock;

struct Options {
  std::filesystem::path root{std::filesystem::current_path()};
  std::filesystem::path out{"research-output/ai-sim"};
  std::vector<int> areas{0, 1, 2};
  int runs{4};
  double seconds{300.0};
  double hz{60.0};        // the battle screen's logic rate (desktop_main.cpp)
  double max_wall{600.0}; // a run stops after this much real time
  int jobs{};             // worker threads (0: one per two hardware threads)
  std::uint32_t seed{1};
  bool trace{};
  std::optional<WatchArea> watch;
  std::optional<WatchArea> nav_dump;  // print the grid here and stop
  std::optional<WatchArea> floor_map; // print the floor here and stop
  std::optional<float> trap_scan;     // list the grid's traps at this jump rise and stop
  ArenaOptions arena;
};

const char *const usage =
    "phantomcrash_ai_sim [--root DIR] [--out DIR] [--areas 0,1,2] [--runs N]\n"
    "  [--seconds S] [--day D] [--robots N] [--player none|stand|patrol]\n"
    "  [--player-entrant E] [--seed S] [--hz HZ] [--jobs N] [--max-wall S]\n"
    "  [--trace] [--watch X,Y,Z,R] [--nav-dump X,Y,Z,R] [--floor-map X,Y,Z,R] [--trap-scan RISE] "
    "[--background] "
    "[--no-poses]\n"
    "  [--no-doors] [--no-supply]\n";

std::vector<int> int_list(const std::string &text) {
  std::vector<int> values;
  std::stringstream stream(text);
  std::string item;
  while (std::getline(stream, item, ',')) {
    values.push_back(std::stoi(item));
  }
  return values;
}

WatchArea watch_area(const std::string &value, const std::string &option) {
  std::stringstream text(value);
  WatchArea area;
  char comma = 0;
  text >> area.at.x >> comma >> area.at.y >> comma >> area.at.z >> comma >> area.radius;
  if (!text) {
    throw std::runtime_error(option + " takes X,Y,Z,R");
  }
  return area;
}

PlayerMode player_mode(const std::string &mode) {
  if (mode == "none") {
    return PlayerMode::none;
  }
  if (mode == "stand") {
    return PlayerMode::stand;
  }
  if (mode == "patrol") {
    return PlayerMode::patrol;
  }
  throw std::runtime_error("--player is none, stand or patrol, not " + mode);
}

Options parse(const int argc, char **argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    const auto value = [&]() -> std::string {
      if (++index >= argc) {
        throw std::runtime_error(argument + " needs a value");
      }
      return argv[index];
    };
    if (argument == "--root") {
      options.root = value();
    } else if (argument == "--out") {
      options.out = value();
    } else if (argument == "--areas") {
      options.areas = int_list(value());
    } else if (argument == "--runs") {
      options.runs = std::stoi(value());
    } else if (argument == "--seconds") {
      options.seconds = std::stod(value());
    } else if (argument == "--day") {
      options.arena.day = std::stoi(value());
    } else if (argument == "--robots") {
      options.arena.robots = std::stoi(value());
    } else if (argument == "--player") {
      options.arena.player = player_mode(value());
    } else if (argument == "--player-entrant") {
      options.arena.player_entrant = std::stoi(value());
    } else if (argument == "--seed") {
      options.seed = static_cast<std::uint32_t>(std::stoul(value()));
    } else if (argument == "--hz") {
      options.hz = std::stod(value());
    } else if (argument == "--jobs") {
      options.jobs = std::stoi(value());
    } else if (argument == "--max-wall") {
      options.max_wall = std::stod(value());
    } else if (argument == "--trace") {
      options.trace = true;
    } else if (argument == "--watch") {
      options.watch = watch_area(value(), argument);
    } else if (argument == "--nav-dump") {
      options.nav_dump = watch_area(value(), argument);
    } else if (argument == "--floor-map") {
      options.floor_map = watch_area(value(), argument);
    } else if (argument == "--trap-scan") {
      options.trap_scan = std::stof(value());
    } else if (argument == "--background") {
      options.arena.background = true;
    } else if (argument == "--no-poses") {
      options.arena.poses = false;
    } else if (argument == "--no-doors") {
      options.arena.doors = false;
    } else if (argument == "--no-supply") {
      options.arena.supply = false;
    } else if (argument == "--help" || argument == "-h") {
      std::cout << usage;
      std::exit(0);
    } else {
      throw std::runtime_error("unknown option " + argument + "\n" + usage);
    }
  }
  if (options.runs <= 0 || options.seconds <= 0.0 || options.hz <= 0.0) {
    throw std::runtime_error("--runs, --seconds and --hz must be positive");
  }
  return options;
}

struct Job {
  int index{}, area{};
};

struct Result {
  bool ran{};
  RunStats run;
  std::vector<LifeStats> lives;
  std::vector<StuckPoint> stuck;
  std::string trace, watch;
};

Result simulate(SimData &data, const Options &options, const Job job) {
  Result result;
  const auto started = Clock::now();
  const auto seed = options.seed + static_cast<std::uint32_t>(job.index) * 7919U;
  SimArena arena(data, options.arena, job.area, seed);
  if (!arena.ready()) {
    return result;
  }
  std::ostringstream trace, watch;
  Probe probe(job.index, arena, ProbeTuning{}, options.trace ? &trace : nullptr);
  const auto step = static_cast<float>(1.0 / options.hz);
  const auto ticks = static_cast<long long>(options.seconds * options.hz + 0.5);
  long long tick = 0;
  for (; tick < ticks; ++tick) {
    arena.tick(step);
    probe.observe(static_cast<double>(tick + 1) * step, step);
    if (options.watch) {
      write_watch(watch, job.index, static_cast<double>(tick + 1) * step, arena.battle(),
                  *options.watch);
    }
    if ((tick & 1023) == 0 &&
        std::chrono::duration<double>(Clock::now() - started).count() > options.max_wall) {
      std::cerr << "run " << job.index << " hit --max-wall; stopped early\n";
      break;
    }
  }
  probe.finish(static_cast<double>(tick) * step,
               std::chrono::duration<double>(Clock::now() - started).count());
  result.ran = true;
  result.run = probe.run();
  result.run.day = options.arena.day;
  result.lives = probe.lives();
  result.stuck = probe.stuck_points();
  result.trace = trace.str();
  result.watch = watch.str();
  return result;
}

std::ofstream open(const std::filesystem::path &path) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("cannot write " + path.string());
  }
  return file;
}

void write(const Options &options, const std::vector<Result> &results) {
  std::filesystem::create_directories(options.out);
  std::ofstream lives = open(options.out / "lives.jsonl");
  std::ofstream runs = open(options.out / "runs.jsonl");
  std::ofstream stuck = open(options.out / "stuck.csv");
  write_stuck_header(stuck);
  std::optional<std::ofstream> trace;
  if (options.trace) {
    trace = open(options.out / "trace.csv");
    write_trace_header(*trace);
  }
  std::optional<std::ofstream> watch;
  if (options.watch) {
    watch = open(options.out / "watch.csv");
    write_watch_header(*watch);
  }
  for (const Result &result : results) {
    if (!result.ran) {
      continue;
    }
    write_run(runs, result.run);
    for (const LifeStats &life : result.lives) {
      write_life(lives, life);
    }
    for (const StuckPoint &point : result.stuck) {
      write_stuck(stuck, point);
    }
    if (trace) {
      *trace << result.trace;
    }
    if (watch) {
      *watch << result.watch;
    }
  }
}

int run(const Options &options) {
  if (options.nav_dump || options.floor_map || options.trap_scan) {
    SimData data(options.root);
    for (const int area : options.areas) {
      const bool dumped =
          options.trap_scan  ? scan_traps(data, options.arena, area, *options.trap_scan, std::cout)
          : options.nav_dump ? dump_nav(data, options.arena, area, *options.nav_dump, std::cout)
                             : dump_floor(data, options.arena, area, *options.floor_map, std::cout);
      if (!dumped) {
        std::printf("area %d: nobody out on day %d\n", area, options.arena.day);
      }
    }
    return 0;
  }
  std::vector<Job> jobs;
  for (const int area : options.areas) {
    for (int repeat = 0; repeat < options.runs; ++repeat) {
      jobs.push_back({static_cast<int>(jobs.size()), area});
    }
  }
  const int hardware = static_cast<int>(std::max(2U, std::thread::hardware_concurrency()));
  const int workers =
      std::clamp(options.jobs > 0 ? options.jobs : hardware / 2, 1, static_cast<int>(jobs.size()));
  std::vector<Result> results(jobs.size());
  std::atomic<std::size_t> next{0};
  std::mutex report;
  std::exception_ptr failure;
  const auto started = Clock::now();
  const auto work = [&] {
    try {
      SimData data(options.root);
      for (std::size_t job = next++; job < jobs.size(); job = next++) {
        results[job] = simulate(data, options, jobs[job]);
        const RunStats &stats = results[job].run;
        const std::scoped_lock guard(report);
        if (!results[job].ran) {
          std::printf("run %d area %d: nobody out on day %d\n", jobs[job].index, jobs[job].area,
                      options.arena.day);
        } else {
          std::printf("run %d area %d (%s, contest %d): %.0f s in %.1f s (%.0fx) lives %d "
                      "kills %d shots %d\n",
                      stats.run, stats.area, stats.stage.c_str(), stats.contest, stats.seconds,
                      stats.wall_seconds, stats.seconds / std::max(stats.wall_seconds, 1e-6),
                      stats.lives, stats.kills, stats.shots);
        }
        std::fflush(stdout);
      }
    } catch (...) {
      const std::scoped_lock guard(report);
      failure = std::current_exception();
      next = jobs.size();
    }
  };
  std::vector<std::thread> threads;
  for (int index = 0; index < workers; ++index) {
    threads.emplace_back(work);
  }
  for (std::thread &thread : threads) {
    thread.join();
  }
  if (failure) {
    std::rethrow_exception(failure);
  }
  write(options, results);
  double game = 0.0;
  for (const Result &result : results) {
    game += result.ran ? result.run.seconds : 0.0;
  }
  const double wall = std::chrono::duration<double>(Clock::now() - started).count();
  std::printf("Total: %.0f game seconds in %.1f s on %d threads (%.0fx real time)\n", game, wall,
              workers, game / std::max(wall, 1e-6));
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    return run(parse(argc, argv));
  } catch (const std::exception &error) {
    std::cerr << "phantomcrash_ai_sim: " << error.what() << '\n';
    return 1;
  }
}
