#pragma once

#include "phantomcrash/input.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

struct Options {
  // Folder holding data/, mods/ and artifacts/; found automatically when absent.
  std::optional<std::filesystem::path> root;
  // Overrides the saved setting for this run.
  std::optional<bool> fullscreen;
  // The settings file (and quest save) to use instead of the one in the
  // user's preferences folder, e.g. to try a save.
  std::optional<std::filesystem::path> settings;
  // Smoke test: load everything, run this many frames hidden, then exit.
  int smoke_frames{};
  // Hidden desktop asset/rollback verification; never reads or writes profiles.
  bool online_resource_check{};
  // Hidden two-peer online match timed against 144 fps; game seconds measured.
  double online_stress_seconds{};
  struct Online {
    bool host{};
    std::string address;
    std::uint16_t port{};
  };
  std::optional<Online> online;
  // Benchmark: run this many seconds in real time, uncapped and without
  // vsync, then print the frame times (--press frames count logic ticks).
  double benchmark_seconds{};
  // Smoke test only: save the last rendered frame as a BMP.
  std::optional<std::filesystem::path> capture;
  // Window client size in pixels for this run (e.g. to check other aspects).
  std::optional<std::pair<int, int>> size;
  // The live RUMBLING's robots an area for this run (Settings::rumble_robots).
  std::optional<int> rumble_robots;
  // Screen to start on: intro (default), title, main_menu or a placeholder.
  std::string start{"intro"};
  // Smoke test only: button presses injected at given frames, to drive menus;
  // a range of frames holds the buttons down through it.
  struct Press {
    int frame{};
    int last{}; // the last frame held; `frame` for a tap
    Buttons buttons{};
    int port{}; // the controller port, 0 to 3
  };
  std::vector<Press> presses;
  // Smoke test only: the mouse moved to a canvas point at a given frame,
  // and clicked there.
  struct Point {
    int frame{};
    std::array<float, 2> canvas{};
    bool click{};
  };
  std::vector<Point> points;
  // Renders one garage robot record standing alone on a transparent
  // background to a square PNG, then exits (an icon or a promotional shot).
  // The camera orbits the robot: yaw turns it from straight ahead, pitch
  // raises it, both in degrees; it looks at `height` above the ground from
  // `distance` away.
  struct RobotShot {
    int record{};
    std::filesystem::path file;
    float yaw_degrees{-30.0F};
    float pitch_degrees{-12.0F};
    float distance{11.0F};
    float height{2.4F};
    int size{1024};
  };
  std::optional<RobotShot> robot_shot;
  bool help{};
};

// Throws std::runtime_error on an unknown or incomplete argument.
[[nodiscard]] Options parse_options(int argc, char **argv);
[[nodiscard]] const char *usage();

} // namespace phantomcrash
