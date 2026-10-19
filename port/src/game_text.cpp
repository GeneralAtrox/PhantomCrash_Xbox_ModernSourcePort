#include "phantomcrash/game_text.hpp"

#include <charconv>
#include <format>
#include <optional>
#include <stdexcept>
#include <variant>

namespace phantomcrash {
namespace {

constexpr std::string_view reference_open = "{xbe:0x";

struct Reference {
  std::size_t start{}, end{}; // the whole {xbe:0x...} in the text
  std::uint32_t address{};
};

// The reference starting at or after `from`; nullopt when there is none.
std::optional<Reference> next_reference(std::string_view text, std::size_t from) {
  while (true) {
    const std::size_t start = text.find(reference_open, from);
    if (start == std::string_view::npos) {
      return std::nullopt;
    }
    const std::size_t digits = start + reference_open.size();
    const std::size_t close = text.find('}', digits);
    if (close == std::string_view::npos) {
      return std::nullopt;
    }
    std::uint32_t address{};
    const char *first = text.data() + digits;
    const char *last = text.data() + close;
    const auto [end, error] = std::from_chars(first, last, address, 16);
    if (error == std::errc{} && end == last && first != last) {
      return Reference{start, close + 1, address};
    }
    from = start + 1;
  }
}

std::uint32_t parse_address(const std::string &key) {
  std::string_view digits = key;
  if (digits.starts_with("0x") || digits.starts_with("0X")) {
    digits.remove_prefix(2);
  }
  std::uint32_t address{};
  const auto [end, error] =
      std::from_chars(digits.data(), digits.data() + digits.size(), address, 16);
  if (error != std::errc{} || end != digits.data() + digits.size() || digits.empty()) {
    throw std::runtime_error(std::string(game_text_table) + ": not an address: " + key);
  }
  return address;
}

} // namespace

std::vector<std::uint32_t> game_text_references(std::string_view text) {
  std::vector<std::uint32_t> out;
  for (auto found = next_reference(text, 0); found; found = next_reference(text, found->end)) {
    out.push_back(found->address);
  }
  return out;
}

std::string game_text_reference(std::uint32_t address) {
  return std::format("{{xbe:0x{:06x}}}", address);
}

std::vector<std::filesystem::path> game_text_sources(const std::filesystem::path &root) {
  return {root / "data", root / "mods" / "data"};
}

GameText GameText::parse(const json::JsonValue &table) {
  GameText out;
  const auto *strings = table.find("strings");
  if (strings == nullptr) {
    throw std::runtime_error(std::string(game_text_table) + " has no \"strings\"");
  }
  for (const auto &[key, value] : strings->object()) {
    out.strings_[parse_address(key)] = value.string();
  }
  return out;
}

const std::string *GameText::find(std::uint32_t address) const {
  const auto found = strings_.find(address);
  return found == strings_.end() ? nullptr : &found->second;
}

std::string GameText::resolve(std::string_view text) const {
  std::string out;
  std::size_t copied = 0;
  for (auto found = next_reference(text, 0); found; found = next_reference(text, found->end)) {
    const std::string *string = find(found->address);
    if (string == nullptr) {
      throw std::runtime_error(
          std::format("{} is not in {}: run phantomcrash_extract install --convert to read the "
                      "game's text again",
                      text.substr(found->start, found->end - found->start), game_text_table));
    }
    out.append(text.substr(copied, found->start - copied));
    out.append(*string);
    copied = found->end;
  }
  out.append(text.substr(copied));
  return out;
}

void GameText::resolve(json::JsonValue &value) const {
  if (auto *string = std::get_if<std::string>(&value.value)) {
    if (string->find(reference_open) != std::string::npos) {
      *string = resolve(*string);
    }
  } else if (auto *array = std::get_if<json::JsonValue::Array>(&value.value)) {
    for (auto &item : *array) {
      resolve(item);
    }
  } else if (auto *object = std::get_if<json::JsonValue::Object>(&value.value)) {
    for (auto &member : *object) {
      resolve(member.second);
    }
  }
}

bool has_game_text_references(const json::JsonValue &value) {
  if (const auto *string = std::get_if<std::string>(&value.value)) {
    return next_reference(*string, 0).has_value();
  }
  if (const auto *array = std::get_if<json::JsonValue::Array>(&value.value)) {
    for (const auto &item : *array) {
      if (has_game_text_references(item)) {
        return true;
      }
    }
  } else if (const auto *object = std::get_if<json::JsonValue::Object>(&value.value)) {
    for (const auto &member : *object) {
      if (has_game_text_references(member.second)) {
        return true;
      }
    }
  }
  return false;
}

} // namespace phantomcrash
