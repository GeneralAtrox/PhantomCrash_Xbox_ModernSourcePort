#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/game_text.hpp"
#include "phantomcrash/slai_content.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace phantomcrash {
namespace {

bool same_name(const std::string &left, const std::string &right) {
  return std::ranges::equal(left, right, [](unsigned char a, unsigned char b) {
    return std::tolower(a) == std::tolower(b);
  });
}

// `folder / relative`, matching each component case-insensitively when the
// exact spelling is absent.
std::optional<std::filesystem::path> locate(const std::filesystem::path &folder,
                                            const std::filesystem::path &relative) {
  std::error_code error;
  const auto exact = folder / relative;
  if (std::filesystem::is_regular_file(exact, error)) {
    return exact;
  }
  std::filesystem::path current = folder;
  for (const auto &component : relative) {
    const auto next = current / component;
    if (std::filesystem::exists(next, error)) {
      current = next;
      continue;
    }
    std::optional<std::filesystem::path> match;
    for (std::filesystem::directory_iterator entry(current, error), end; !error && entry != end;
         entry.increment(error)) {
      if (same_name(entry->path().filename().string(), component.string())) {
        match = entry->path();
        break;
      }
    }
    if (!match) {
      return std::nullopt;
    }
    current = *match;
  }
  return std::filesystem::is_regular_file(current, error) ? std::optional(current) : std::nullopt;
}

std::filesystem::path checked_relative(std::string_view relative) {
  const std::filesystem::path path{std::u8string(relative.begin(), relative.end())};
  if (relative.empty() || path.is_absolute() || path.has_root_name() ||
      std::ranges::any_of(path, [](const auto &part) { return part == ".."; })) {
    throw std::runtime_error("Data path must be relative and stay inside its root: " +
                             std::string(relative));
  }
  return path;
}

} // namespace

struct DataRoots::TextCache {
  std::once_flag once;
  GameText table;
};

DataRoots::DataRoots(std::filesystem::path data, std::filesystem::path mods,
                     std::vector<std::filesystem::path> asset_roots)
    : data_(std::move(data)), mods_(std::move(mods)), asset_roots_(std::move(asset_roots)),
      text_(std::make_shared<TextCache>()) {}

DataRoots DataRoots::standard(const std::filesystem::path &root) {
  return DataRoots(root / "data", root / "mods",
                   {root / "mods", root / "artifacts" / "europe" / "assets",
                    root / "artifacts" / "europe" / "files" / "Media", root / "data" / "assets"});
}

DataRoots DataRoots::shipped(const std::filesystem::path &root) {
  return DataRoots(root / "data", {},
                   {root / "artifacts" / "europe" / "assets",
                    root / "artifacts" / "europe" / "files" / "Media", root / "data" / "assets"});
}

DataRoots DataRoots::configured(const std::filesystem::path &root, bool slai) {
  auto result = legacy_slai(root) ? shipped(root) : standard(root);
  if (slai) {
    if (!slai_installed(root)) {
      throw std::runtime_error("Install S.L.A.I. from its ISO in Settings before enabling it");
    }
    result.content_ = root / "artifacts/slai/assets";
    const auto at = result.asset_roots_.begin() + (result.mods_.empty() ? 0 : 1);
    result.asset_roots_.insert(at, result.content_);
  }
  return result;
}

std::optional<std::filesystem::path>
DataRoots::find_root(const std::vector<std::filesystem::path> &starts) {
  std::error_code error;
  for (const auto &start : starts) {
    auto current = std::filesystem::weakly_canonical(start, error);
    if (error) {
      continue;
    }
    while (true) {
      if (std::filesystem::is_regular_file(current / "data" / "boot" / "intro.json", error)) {
        return current;
      }
      if (!current.has_parent_path() || current.parent_path() == current) {
        break;
      }
      current = current.parent_path();
    }
  }
  return std::nullopt;
}

std::optional<std::filesystem::path> DataRoots::find_asset(std::string_view relative) const {
  const auto path = checked_relative(relative);
  for (const auto &root : asset_roots_) {
    if (auto found = locate(root, path)) {
      return found;
    }
  }
  return std::nullopt;
}

std::filesystem::path DataRoots::asset(std::string_view relative) const {
  if (auto found = find_asset(relative)) {
    return *found;
  }
  std::string message = "Missing asset " + std::string(relative) + "; searched";
  for (const auto &root : asset_roots_) {
    message += " " + root.string();
  }
  throw std::runtime_error(message);
}

bool DataRoots::has_document(std::string_view relative) const {
  std::error_code error;
  return std::filesystem::is_regular_file(data_ / checked_relative(relative), error);
}

json::JsonValue DataRoots::document(std::string_view relative) const {
  const auto path = checked_relative(relative);
  auto result = json::read_file(data_ / path);
  if (!content_.empty()) {
    if (auto overlay = locate(content_ / "data", path)) {
      json::merge(result, json::read_file(*overlay));
    }
  }
  if (!mods_.empty()) {
    if (auto overlay = locate(mods_ / "data", path)) {
      json::merge(result, json::read_file(*overlay));
    }
  }
  if (has_game_text_references(result)) {
    game_text().resolve(result);
  }
  return result;
}

const GameText &DataRoots::game_text() const {
  // Without the table every reference reports itself missing, naming the fix.
  std::call_once(text_->once, [this] {
    if (const auto table = find_asset(game_text_table)) {
      text_->table = GameText::parse(json::read_file(*table));
    }
  });
  return text_->table;
}

} // namespace phantomcrash
