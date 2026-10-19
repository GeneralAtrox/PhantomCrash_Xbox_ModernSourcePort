#pragma once

// Checked member access for the data documents: a missing or malformed value
// throws with the key's name, and callers add which document and entry.

#include "phantomcrash/json.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace phantomcrash::json {

[[nodiscard]] inline const JsonValue &required(const JsonValue &value, const char *key) {
  const auto *found = value.find(key);
  if (found == nullptr) {
    throw std::runtime_error(std::string("needs \"") + key + "\"");
  }
  return *found;
}

template <std::size_t N>
[[nodiscard]] std::array<float, N> numbers(const JsonValue &value, const char *what) {
  const auto &items = value.array();
  if (items.size() != N) {
    throw std::runtime_error(std::string(what) + " needs " + std::to_string(N) + " numbers");
  }
  std::array<float, N> result{};
  for (std::size_t index = 0; index < N; ++index) {
    result[index] = static_cast<float>(items[index].number());
  }
  return result;
}

// Parses a member with `parse`, naming it in the error.
template <typename Parse>
[[nodiscard]] auto member(const JsonValue &value, const char *key, Parse parse) {
  try {
    return parse(required(value, key));
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string(key) + ": " + error.what());
  }
}

} // namespace phantomcrash::json
