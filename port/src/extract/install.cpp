#include "install.hpp"

#include "binary.hpp"
#include "parallel.hpp"
#include "phantomcrash/game_data.hpp"
#include "phantomcrash/game_text.hpp"
#include "phantomcrash/json.hpp"
#include "sha256.hpp"

#include <format>
#include <fstream>

namespace phantomcrash::extract {
namespace {

namespace fs = std::filesystem;

// Staging folders, beside the folders they replace; only this tool writes them.
constexpr const char *extract_staging = "extract.partial";
constexpr const char *convert_staging = "assets.partial";
constexpr const char *replaced_suffix = ".old";
// Room the converted assets need beyond the disc's files (about 0.4 GB).
constexpr std::uintmax_t assets_space = 1ull << 30;
constexpr std::size_t hash_chunk = 8 * 1024 * 1024;

void remove_owned(const fs::path &path) {
  std::error_code error;
  fs::remove_all(path, error);
  if (error) {
    throw std::runtime_error(
        std::format("Could not remove {}: {}", path.string(), error.message()));
  }
}

// Moves `staged` into place; the old `target` is moved aside first and put
// back if the move fails.
void replace_directory(const fs::path &staged, const fs::path &target) {
  const fs::path old = target.parent_path() / (target.filename().string() + replaced_suffix);
  remove_owned(old);
  std::error_code error;
  if (fs::exists(target)) {
    fs::rename(target, old, error);
    if (error) {
      throw std::runtime_error(
          std::format("Could not replace {} ({}). Close the game and try again.", target.string(),
                      error.message()));
    }
  }
  fs::rename(staged, target, error);
  if (error) {
    std::error_code restore;
    if (fs::exists(old)) {
      fs::rename(old, target, restore);
    }
    throw std::runtime_error(
        std::format("Could not move {} into place: {}", staged.string(), error.message()));
  }
  remove_owned(old);
}

void require_space(const fs::path &folder, const std::uintmax_t needed) {
  std::error_code error;
  const auto space = fs::space(folder, error);
  if (!error && space.available < needed) {
    throw std::runtime_error(
        std::format("Not enough free disk space in {}: {:.1f} GB needed, {:.1f} GB free",
                    folder.string(), needed / 1e9, space.available / 1e9));
  }
}

// Staging and replaced folders left by an interrupted run.
void remove_leftovers(const GameDataPaths &paths) {
  for (const fs::path &folder : {paths.europe / extract_staging, paths.europe / convert_staging}) {
    remove_owned(folder);
  }
  for (const fs::path &target : {paths.files, paths.metadata, paths.assets}) {
    const fs::path old = target.parent_path() / (target.filename().string() + replaced_suffix);
    if (!fs::exists(target) && fs::exists(old)) {
      fs::rename(old, target); // the run stopped between its two renames
    }
    remove_owned(old);
  }
}

void extract(const GameDataPaths &paths, const fs::path &disc, const Progress &report) {
  if (!fs::is_regular_file(disc)) {
    throw std::runtime_error("Disc image not found: " + disc.string());
  }
  fs::create_directories(paths.europe);
  const fs::path staging = paths.europe / extract_staging;
  remove_owned(staging);
  require_space(paths.europe, fs::file_size(disc) + assets_space);
  report("Extracting " + disc.filename().string());
  extract_disc(disc, staging, report);
  replace_directory(staging / "files", paths.files);
  replace_directory(staging / "metadata", paths.metadata);
  remove_owned(staging);
}

void convert(const fs::path &root, const GameDataPaths &paths, const Progress &report) {
  const fs::path staging = paths.europe / convert_staging;
  remove_owned(staging);
  report("Converting the game data");
  convert_assets(paths.files, staging, false, game_text_sources(root), report);
  replace_directory(staging, paths.assets);
}

std::string file_sha256(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  Sha256 hash;
  Bytes chunk(hash_chunk);
  while (in) {
    in.read(reinterpret_cast<char *>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
    hash.update({chunk.data(), static_cast<std::size_t>(in.gcount())});
  }
  return hash.hex();
}

} // namespace

std::string verify_extracted_files(const fs::path &root) {
  const GameDataPaths paths = game_data_paths(root);
  const json::JsonValue record = json::read_file(paths.metadata / "disc.json");
  const auto &files = record.find("files")->array();
  std::vector<std::string> mismatches(files.size());
  parallel_for(files.size(), [&](const std::size_t index) {
    const std::string &name = files[index].find("path")->string();
    const fs::path path = paths.files / utf8_path(std::string_view(name).substr(1));
    if (file_sha256(path) != files[index].find("sha256")->string()) {
      mismatches[index] = name;
    }
  });
  for (const std::string &name : mismatches) {
    if (!name.empty()) {
      return name;
    }
  }
  return {};
}

InstallResult install_game_data(const fs::path &root, const InstallOptions &options,
                                const Progress &progress) {
  const Progress report = progress ? progress : [](const std::string &) {};
  const GameDataPaths paths = game_data_paths(root);
  remove_leftovers(paths);
  const GameDataStatus status = game_data_status(root);
  InstallResult result;
  bool files_valid = status.files;
  if (files_valid && options.verify) {
    report("Verifying the extracted files");
    if (const std::string changed = verify_extracted_files(root); !changed.empty()) {
      report("Changed since extraction: " + changed);
      files_valid = false;
    }
  }
  if (!files_valid) {
    if (!options.disc) {
      throw DiscNeeded(
          status.files
              ? "The extracted files have changed; the disc image is needed to extract them again."
              : "The disc image is needed: " + status.problem + ".");
    }
    extract(paths, *options.disc, report);
    result.extracted = true;
  }
  if (result.extracted || !status.assets || options.convert) {
    if (!status.assets && !result.extracted) {
      report("Updating the converted assets: " + status.problem);
    }
    convert(root, paths, report);
    result.converted = true;
  }
  return result;
}

} // namespace phantomcrash::extract
