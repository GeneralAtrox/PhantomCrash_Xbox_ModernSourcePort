// tables/text.json: the game's own text that the port's data files reference
// (phantomcrash/game_text.hpp), read from the player's executable so the
// repository and the release carry none of it.
#include "convert_steps.hpp"
#include "text.hpp"
#include "phantomcrash/game_text.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <map>
#include <stdexcept>

namespace phantomcrash::extract {
namespace {

namespace fs = std::filesystem;

bool is_json(const fs::path &path) {
  std::string extension = path.extension().string();
  std::ranges::transform(extension, extension.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension == ".json";
}

// Every referenced address with the first file that names it, for messages.
std::map<std::uint32_t, fs::path> references(const std::vector<fs::path> &sources) {
  std::map<std::uint32_t, fs::path> out;
  for (const auto &source : sources) {
    std::error_code error;
    if (!fs::is_directory(source, error)) {
      continue;
    }
    std::vector<fs::path> files;
    for (const auto &entry : fs::recursive_directory_iterator(source)) {
      if (entry.is_regular_file() && is_json(entry.path())) {
        files.push_back(entry.path());
      }
    }
    std::ranges::sort(files);
    for (const auto &file : files) {
      const Bytes bytes = read_file(file);
      const std::string_view contents(reinterpret_cast<const char *>(bytes.data()), bytes.size());
      for (const std::uint32_t address : game_text_references(contents)) {
        out.try_emplace(address, file);
      }
    }
  }
  return out;
}

} // namespace

Json write_game_text(const XbeImage &image, const std::vector<fs::path> &sources,
                     const fs::path &output) {
  Json strings = object();
  for (const auto &[address, file] : references(sources)) {
    std::string string;
    try {
      string = plain(image.raw_string(address));
    } catch (const std::exception &failure) {
      throw std::runtime_error(std::format("{}: {} names no text in the executable ({})",
                                           utf8_name(file), game_text_reference(address),
                                           failure.what()));
    }
    put(strings, std::format("0x{:06x}", address), text(std::move(string)));
  }
  const std::size_t count = strings.object().size();
  write_json(output / game_text_table,
             object({{"notes", text("The game's text that the data files reference as "
                                    "{xbe:0x...}: the Shift-JIS string at that address in "
                                    "default.xbe, as NFKC and trimmed.")},
                     {"strings", std::move(strings)}}),
             1, false);
  return integer(count);
}

} // namespace phantomcrash::extract
