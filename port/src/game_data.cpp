#include "phantomcrash/game_data.hpp"

#include "phantomcrash/json.hpp"

#include <exception>
#include <system_error>

namespace phantomcrash {
namespace {

// Every file disc.json lists exists at its recorded size.
std::string check_files(const GameDataPaths &paths) {
  std::error_code error;
  const auto disc = paths.metadata / "disc.json";
  if (!std::filesystem::is_regular_file(disc, error)) {
    return "the disc has not been extracted (no metadata/disc.json)";
  }
  try {
    const json::JsonValue record = json::read_file(disc);
    const auto *files = record.find("files");
    if (files == nullptr || files->array().empty()) {
      return "metadata/disc.json lists no files";
    }
    for (const auto &file : files->array()) {
      const std::string &name = file.find("path")->string();
      const std::u8string relative(name.begin() + (name.starts_with('/') ? 1 : 0), name.end());
      const auto path = paths.files / std::filesystem::path(relative);
      const auto size = std::filesystem::file_size(path, error);
      if (error || size != static_cast<std::uintmax_t>(file.find("size")->number())) {
        return "extracted file missing or changed: " + name;
      }
    }
  } catch (const std::exception &failure) {
    return std::string("metadata/disc.json is unreadable: ") + failure.what();
  }
  return {};
}

} // namespace

GameDataPaths game_data_paths(const std::filesystem::path &root) {
  const auto europe = root / "artifacts" / "europe";
  return {europe, europe / "files", europe / "metadata", europe / "assets"};
}

GameDataStatus game_data_status(const std::filesystem::path &root) {
  const GameDataPaths paths = game_data_paths(root);
  GameDataStatus status;
  status.problem = check_files(paths);
  status.files = status.problem.empty();

  std::error_code error;
  const auto manifest = paths.assets / "manifest.json";
  std::string assets_problem;
  if (!std::filesystem::is_regular_file(manifest, error)) {
    assets_problem = "the assets have not been converted (no assets/manifest.json)";
  } else {
    try {
      const json::JsonValue record = json::read_file(manifest);
      status.assets_version = static_cast<int>(record.number_or("converter_version", 1));
      if (!record.boolean_or("complete", false)) {
        assets_problem = "the last conversion did not finish";
      } else if (status.assets_version != converted_assets_version) {
        assets_problem = "the assets were converted by another version (" +
                         std::to_string(status.assets_version) + ", this build reads " +
                         std::to_string(converted_assets_version) + ")";
      }
    } catch (const std::exception &failure) {
      assets_problem = std::string("assets/manifest.json is unreadable: ") + failure.what();
    }
  }
  status.assets = assets_problem.empty();
  if (status.problem.empty()) {
    status.problem = assets_problem;
  }
  return status;
}

} // namespace phantomcrash
