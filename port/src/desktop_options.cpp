#include "desktop_options.hpp"

#include "phantomcrash/settings.hpp"

#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>

namespace phantomcrash {
namespace {

// "<yaw>,<pitch>,<distance>,<height>[,<size>]" into `shot`.
void parse_robot_view(const std::string_view text, Options::RobotShot &shot) {
  float values[5] = {shot.yaw_degrees, shot.pitch_degrees, shot.distance, shot.height,
                     static_cast<float>(shot.size)};
  std::size_t count = 0;
  for (std::size_t start = 0; start <= text.size() && count < 5; ++count) {
    const auto comma = std::min(text.find(',', start), text.size());
    const auto result = std::from_chars(text.data() + start, text.data() + comma, values[count]);
    if (result.ec != std::errc{} || result.ptr != text.data() + comma) {
      throw std::runtime_error("Invalid --robot-view: " + std::string(text));
    }
    start = comma + 1;
  }
  if (count < 4 || values[2] <= 0.0F || values[4] < 16.0F || values[4] > 8192.0F) {
    throw std::runtime_error("Invalid --robot-view: " + std::string(text));
  }
  shot.yaw_degrees = values[0];
  shot.pitch_degrees = values[1];
  shot.distance = values[2];
  shot.height = values[3];
  shot.size = static_cast<int>(values[4]);
}

} // namespace

const char *usage() {
  return "Usage: phantomcrash_port [--root <folder>] [--fullscreen | --windowed] "
         "[--size <width>x<height>] [--settings <file.json>] [--rumble-robots <0-16>] "
         "[--smoke-test [frames] [--capture <file.bmp>]] [--benchmark [seconds]]\n"
         "[--online-resource-check] [--online-stress-check [seconds]]\n"
         "[--online-host <port> | --online-join <IPv4:port>]\n"
         "\n"
         "The root folder holds data/, mods/ and artifacts/europe (the extracted and\n"
         "converted game files). No game files are bundled.\n";
}

Options parse_options(const int argc, char **argv) {
  Options options;
  Options::RobotShot robot_shot; // a --robot-view given before --render-robot
  bool robot_view = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--root") {
      if (++index >= argc) {
        throw std::runtime_error("--root requires a folder");
      }
      options.root = std::filesystem::path(argv[index]);
    } else if (argument == "--fullscreen") {
      options.fullscreen = true;
    } else if (argument == "--windowed") {
      options.fullscreen = false;
    } else if (argument == "--smoke-test") {
      options.smoke_frames = 120;
      if (index + 1 < argc && argv[index + 1][0] != '-') {
        const std::string value = argv[++index];
        std::size_t used = 0;
        options.smoke_frames = std::stoi(value, &used);
        if (used != value.size() || options.smoke_frames <= 0) {
          throw std::runtime_error("Invalid smoke-test frame count: " + value);
        }
      }
    } else if (argument == "--benchmark") {
      options.benchmark_seconds = 20.0;
      if (index + 1 < argc && argv[index + 1][0] != '-') {
        const std::string value = argv[++index];
        std::size_t used = 0;
        options.benchmark_seconds = std::stod(value, &used);
        if (used != value.size() || options.benchmark_seconds <= 0.0) {
          throw std::runtime_error("Invalid benchmark length: " + value);
        }
      }
    } else if (argument == "--settings") {
      if (++index >= argc) {
        throw std::runtime_error("--settings requires a file");
      }
      options.settings = std::filesystem::path(argv[index]);
    } else if (argument == "--capture") {
      if (++index >= argc) {
        throw std::runtime_error("--capture requires a file");
      }
      options.capture = std::filesystem::path(argv[index]);
    } else if (argument == "--size") {
      if (++index >= argc) {
        throw std::runtime_error("--size requires <width>x<height>");
      }
      const std::string_view text = argv[index];
      const auto separator = text.find('x');
      int width = 0;
      int height = 0;
      const auto parse = [](std::string_view part, int &value) {
        const auto result = std::from_chars(part.data(), part.data() + part.size(), value);
        return result.ec == std::errc{} && result.ptr == part.data() + part.size();
      };
      if (separator == std::string_view::npos || !parse(text.substr(0, separator), width) ||
          !parse(text.substr(separator + 1), height) || width < 320 || height < 240) {
        throw std::runtime_error("Invalid --size: " + std::string(argv[index]));
      }
      options.size = std::pair(width, height);
    } else if (argument == "--rumble-robots") {
      if (++index >= argc) {
        throw std::runtime_error("--rumble-robots requires a count");
      }
      const std::string_view text = argv[index];
      int robots = 0;
      const auto result = std::from_chars(text.data(), text.data() + text.size(), robots);
      if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || robots < 0 ||
          robots > max_rumble_robots) {
        throw std::runtime_error("Invalid --rumble-robots: " + std::string(text));
      }
      options.rumble_robots = robots;
    } else if (argument == "--online-host" || argument == "--online-join") {
      if (options.online || ++index >= argc) {
        throw std::runtime_error("Specify one --online-host <port> or --online-join <IPv4:port>");
      }
      Options::Online online;
      online.host = argument == "--online-host";
      const std::string value = argv[index];
      const auto colon = value.rfind(':');
      if (!online.host && colon == std::string::npos) {
        throw std::runtime_error("--online-join requires IPv4:port");
      }
      online.address = online.host ? "0.0.0.0" : value.substr(0, colon);
      const auto port = online.host ? value : value.substr(colon + 1);
      unsigned number{};
      const auto parsed = std::from_chars(port.data(), port.data() + port.size(), number);
      if (parsed.ec != std::errc{} || parsed.ptr != port.data() + port.size() || !number ||
          number > 65535) {
        throw std::runtime_error("Online port must be from 1 to 65535");
      }
      online.port = static_cast<std::uint16_t>(number);
      options.online = std::move(online);
    } else if (argument == "--online-resource-check") {
      options.online_resource_check = true;
    } else if (argument == "--online-stress-check") {
      options.online_stress_seconds = 180.0;
      if (index + 1 < argc && argv[index + 1][0] != '-') {
        const std::string value = argv[++index];
        std::size_t used = 0;
        options.online_stress_seconds = std::stod(value, &used);
        if (used != value.size() || options.online_stress_seconds < 30.0) {
          throw std::runtime_error("Invalid online stress length (30 seconds or more): " + value);
        }
      }
    } else if (argument == "--start") {
      if (++index >= argc) {
        throw std::runtime_error("--start requires a screen name");
      }
      options.start = argv[index];
    } else if (argument == "--press") {
      if (++index >= argc) {
        throw std::runtime_error("--press requires <frame>[-<last>]:<button>");
      }
      const std::string_view text = argv[index];
      const auto separator = text.find(':');
      int frame = 0;
      int last = 0;
      const auto frame_text = text.substr(0, separator);
      const char *const end = frame_text.data() + frame_text.size();
      auto parsed = std::from_chars(frame_text.data(), end, frame);
      last = frame;
      if (parsed.ec == std::errc{} && parsed.ptr != end && *parsed.ptr == '-') {
        parsed = std::from_chars(parsed.ptr + 1, end, last);
      }
      if (separator == std::string_view::npos || parsed.ec != std::errc{} || parsed.ptr != end ||
          frame < 0 || last < frame) {
        throw std::runtime_error("Invalid --press: " + std::string(text));
      }
      // "@<port>" after the button presses it on that controller port (1-4).
      std::string_view name =
          separator == std::string_view::npos ? std::string_view{} : text.substr(separator + 1);
      int port = 1;
      if (const auto at = name.find('@'); at != std::string_view::npos) {
        const auto port_text = name.substr(at + 1);
        const auto read =
            std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
        if (read.ec != std::errc{} || port < 1 || port > controller_ports) {
          throw std::runtime_error("Invalid port in --press: " + std::string(text));
        }
        name = name.substr(0, at);
      }
      const auto buttons = button_from_name(name);
      if (!buttons) {
        throw std::runtime_error("Unknown button in --press: " + std::string(text));
      }
      options.presses.push_back({frame, last, *buttons, port - 1});
    } else if (argument == "--mouse") {
      if (++index >= argc) {
        throw std::runtime_error("--mouse requires <frame>:<x>,<y>[:click]");
      }
      const std::string_view text = argv[index];
      Options::Point point;
      const char *const end = text.data() + text.size();
      auto parsed = std::from_chars(text.data(), end, point.frame);
      bool valid = parsed.ec == std::errc{} && parsed.ptr != end && *parsed.ptr == ':';
      if (valid) {
        parsed = std::from_chars(parsed.ptr + 1, end, point.canvas[0]);
        valid = parsed.ec == std::errc{} && parsed.ptr != end && *parsed.ptr == ',';
      }
      if (valid) {
        parsed = std::from_chars(parsed.ptr + 1, end, point.canvas[1]);
        const std::string_view rest(parsed.ptr, static_cast<std::size_t>(end - parsed.ptr));
        point.click = rest == ":click";
        valid = parsed.ec == std::errc{} && (rest.empty() || point.click) && point.frame >= 0;
      }
      if (!valid) {
        throw std::runtime_error("Invalid --mouse: " + std::string(text));
      }
      options.points.push_back(point);
    } else if (argument == "--render-robot") {
      if (index + 2 >= argc) {
        throw std::runtime_error("--render-robot requires <record> <file.png>");
      }
      auto &shot = options.robot_shot.emplace(robot_shot);
      const std::string_view text = argv[++index];
      const auto result = std::from_chars(text.data(), text.data() + text.size(), shot.record);
      if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || shot.record < 0) {
        throw std::runtime_error("Invalid --render-robot record: " + std::string(text));
      }
      shot.file = std::filesystem::path(argv[++index]);
    } else if (argument == "--robot-view") {
      if (++index >= argc) {
        throw std::runtime_error(
            "--robot-view requires <yaw>,<pitch>,<distance>,<height>[,<size>]");
      }
      auto &shot = options.robot_shot ? *options.robot_shot : robot_shot;
      parse_robot_view(argv[index], shot);
      robot_view = true;
    } else if (argument == "--help" || argument == "-h") {
      options.help = true;
    } else {
      throw std::runtime_error("Unknown argument: " + std::string(argument));
    }
  }
  if (robot_view && !options.robot_shot) {
    throw std::runtime_error("--robot-view requires --render-robot");
  }
  if (options.capture && options.smoke_frames == 0) {
    throw std::runtime_error("--capture requires --smoke-test");
  }
  if ((!options.presses.empty() || !options.points.empty()) && options.smoke_frames == 0 &&
      options.benchmark_seconds <= 0.0) {
    throw std::runtime_error("--press and --mouse require --smoke-test or --benchmark");
  }
  return options;
}

} // namespace phantomcrash
