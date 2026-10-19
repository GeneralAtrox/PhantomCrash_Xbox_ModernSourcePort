// phantomcrash_extract: extracts the user's disc image and converts its assets
// into the open formats the port loads, with the commands and messages of the
// Python tools (tools/phantomcrash.py), plus `install` and `status` for players.
#include "extract.hpp"
#include "install.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/game_data.hpp"
#include "phantomcrash/game_text.hpp"
#include "xbe.hpp"
#include "xdvdfs.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <format>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace phantomcrash;
using namespace phantomcrash::extract;

namespace {

constexpr const char *usage =
    "usage: phantomcrash_extract <command> [options]\n"
    "  install  [--disc ISO] [--root DIR] [--convert] [--verify] [--pause-on-error]\n"
    "           [--quiet] [--timings]\n"
    "                                             Extract and convert whatever is missing or\n"
    "                                             out of date; exits 3 when the disc is needed\n"
    "  status   [--root DIR]                      Report whether the game data is installed\n"
    "  list     [--disc ISO]                      List every file on the disc\n"
    "  xbe      [--disc ISO] [--file XBE]         Print the XBE header as JSON\n"
    "  extract  [--disc ISO] [--output DIR] [--quiet] [--timings]\n"
    "                                             Extract the disc into artifacts\n"
    "  convert  [--files DIR] [--output DIR] [--replace] [--quiet] [--timings]\n"
    "                                             Convert textures, models, sounds and tables\n"
    "  --timings prefixes each progress line with the seconds elapsed.\n";

// Exit codes beyond 0 (success), 1 (error) and 2 (usage).
constexpr int exit_disc_needed = 3;
constexpr int exit_convert_needed = 4;

struct Arguments {
  std::string command;
  std::map<std::string, std::string> values;
  bool quiet{};
  bool replace{};
  bool timings{};
  bool convert{};
  bool verify{};
  bool pause_on_error{};
};

std::filesystem::path executable_directory() {
  wchar_t buffer[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  return std::filesystem::path(buffer).parent_path();
}

// The folder holding data/, searched from the EXE's folder first so a
// release installs beside itself wherever it is started from; else the
// working directory.
std::filesystem::path repository_root() {
  const std::vector<std::filesystem::path> starts{executable_directory(),
                                                  std::filesystem::current_path()};
  return DataRoots::find_root(starts).value_or(std::filesystem::current_path());
}

// The arguments as UTF-8, so paths outside the ANSI code page survive.
std::vector<std::string> utf8_arguments(int argc, wchar_t **argv) {
  std::vector<std::string> out;
  for (int i = 0; i < argc; ++i) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<std::size_t>(std::max(size, 1)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, text.data(), size, nullptr, nullptr);
    text.resize(text.size() - 1);
    out.push_back(std::move(text));
  }
  return out;
}

std::optional<Arguments> parse(const std::vector<std::string> &argv) {
  if (argv.size() < 2) {
    return std::nullopt;
  }
  Arguments out;
  out.command = argv[1];
  for (std::size_t i = 2; i < argv.size(); ++i) {
    const std::string &option = argv[i];
    if (option == "--quiet") {
      out.quiet = true;
    } else if (option == "--replace") {
      out.replace = true;
    } else if (option == "--timings") {
      out.timings = true;
    } else if (option == "--convert") {
      out.convert = true;
    } else if (option == "--verify") {
      out.verify = true;
    } else if (option == "--pause-on-error") {
      out.pause_on_error = true;
    } else if (option.starts_with("--") && i + 1 < argv.size()) {
      out.values[option.substr(2)] = argv[++i];
    } else {
      return std::nullopt;
    }
  }
  return out;
}

std::optional<std::filesystem::path> optional_value(const Arguments &arguments,
                                                    const std::string &key) {
  const auto found = arguments.values.find(key);
  if (found == arguments.values.end()) {
    return std::nullopt;
  }
  return utf8_path(found->second);
}

std::filesystem::path value(const Arguments &arguments, const std::string &key,
                            const std::filesystem::path &fallback) {
  return optional_value(arguments, key).value_or(fallback);
}

std::string grouped(std::uint64_t number) {
  std::string digits = std::to_string(number);
  for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) {
    digits.insert(static_cast<std::size_t>(i), ",");
  }
  return digits;
}

std::uint64_t as_number(const Json &value) {
  if (const auto *u = std::get_if<std::uint64_t>(&value.value)) {
    return *u;
  }
  return static_cast<std::uint64_t>(std::get<std::int64_t>(value.value));
}

std::string as_text(const Json &value) {
  if (value.is_null()) {
    return "None";
  }
  if (value.is_string()) {
    return value.string();
  }
  return std::to_string(as_number(value));
}

int report_status(const std::filesystem::path &root) {
  const GameDataStatus status = game_data_status(root);
  std::cout << "Game data in " << python_path(game_data_paths(root).europe) << '\n'
            << "  disc files: " << (status.files ? "extracted" : "missing") << '\n'
            << "  assets:     " << (status.assets ? "current" : "out of date")
            << " (converter version " << status.assets_version << ", this build "
            << converted_assets_version << ")\n";
  if (!status.problem.empty()) {
    std::cout << "  " << status.problem << '\n';
  }
  return !status.files ? exit_disc_needed : !status.assets ? exit_convert_needed : 0;
}

int install(const Arguments &arguments, const std::filesystem::path &root, const Progress &report) {
  const InstallOptions options{optional_value(arguments, "disc"), arguments.convert,
                               arguments.verify};
  try {
    const InstallResult result = install_game_data(root, options, report);
    if (!result.extracted && !result.converted) {
      std::cout << "The game data is already installed and current.\n";
    } else {
      std::cout << "Game data installed in " << python_path(game_data_paths(root).europe) << '\n';
    }
    return 0;
  } catch (const DiscNeeded &needed) {
    std::cerr << needed.what() << "\nPass the disc image with --disc, or drop it onto "
              << "\"Install Game Data.bat\".\n";
    return exit_disc_needed;
  }
}

int run(const Arguments &arguments) {
  const auto root = value(arguments, "root", repository_root());
  const auto output_root = game_data_paths(root).europe;
  const auto disc =
      value(arguments, "disc", root.parent_path() / "Games" / "Phantom Crash (Europe).xiso.iso");
  const auto started = std::chrono::steady_clock::now();
  const Progress report =
      arguments.quiet ? Progress{} : [&arguments, started](const std::string &line) {
        if (arguments.timings) {
          const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started;
          std::cout << std::format("[{:7.2f}s] ", elapsed.count());
        }
        std::cout << line << std::endl;
      };
  if (arguments.command == "status") {
    return report_status(root);
  }
  if (arguments.command == "install") {
    return install(arguments, root, report);
  }
  if (arguments.command == "list") {
    XdvdfsImage image(disc);
    for (const auto &entry : image.files()) {
      char size[32];
      std::snprintf(size, sizeof(size), "%12s", grouped(entry.size).c_str());
      std::cout << size << "  " << entry.path << '\n';
    }
    return 0;
  }
  if (arguments.command == "xbe") {
    Bytes data;
    if (const auto file = optional_value(arguments, "file")) {
      data = read_file(*file);
    } else {
      XdvdfsImage image(disc);
      for (const auto &entry : image.files()) {
        if (entry.path == "/default.xbe") {
          data = image.read(entry);
        }
      }
    }
    std::cout << dumps(parse_xbe(data).to_json(), {2, true, false}) << '\n';
    return 0;
  }
  if (arguments.command == "extract") {
    const auto output = value(arguments, "output", output_root);
    const Json result = extract_disc(disc, output, report);
    const Json &disc_record = *result.find("disc");
    const Json &header = *result.find("xbe");
    std::cout << "Extracted " << as_text(*disc_record.find("file_count")) << " files ("
              << grouped(as_number(*disc_record.find("total_bytes"))) << " bytes) to "
              << python_path(output) << "\nXBE " << as_text(*header.find("title_code")) << " '"
              << as_text(*header.find("title_name")) << "' XDK "
              << as_text(*header.find("xdk_build")) << " sha256 " << as_text(*header.find("sha256"))
              << '\n';
    return 0;
  }
  if (arguments.command == "convert") {
    const auto files = value(arguments, "files", output_root / "files");
    const auto output = value(arguments, "output", output_root / "assets");
    const Json manifest =
        convert_assets(files, output, arguments.replace, game_text_sources(root), report);
    std::size_t textures = 0;
    for (const auto &pack : manifest.find("texture_packs")->array()) {
      textures += pack.find("textures")->array().size();
    }
    std::size_t effects = 0;
    for (const auto &bank : manifest.find("sound_banks")->array()) {
      effects += bank.find("effects")->array().size();
    }
    const Json &tables = *manifest.find("tables");
    std::cout << "Converted " << textures << " textures, "
              << manifest.find("models")->array().size() << " models, " << effects << " effects, "
              << as_text(*tables.find("sprites")) << " sprites and "
              << as_text(*tables.find("resources")) << " resources to " << python_path(output)
              << '\n';
    return 0;
  }
  std::cerr << usage;
  return 2;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  SetConsoleOutputCP(CP_UTF8);
  const auto arguments = parse(utf8_arguments(argc, argv));
  if (!arguments) {
    std::cerr << usage;
    return 2;
  }
  int code = 1;
  try {
    code = run(*arguments);
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << '\n';
  }
  // The game runs the installer in a console window of its own, which closes on exit.
  if (code != 0 && arguments->pause_on_error) {
    std::cerr << "\nPress Enter to close this window.";
    std::cin.get();
  }
  return code;
}
