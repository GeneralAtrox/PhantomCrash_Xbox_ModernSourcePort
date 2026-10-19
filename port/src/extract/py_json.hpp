// JSON written exactly as Python's json.dumps writes it, so the C++ extractor's
// tables match the Python reference byte for byte.
#pragma once

#include "phantomcrash/json.hpp"

#include <concepts>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace phantomcrash::extract {

using Json = json::JsonValue;

// Python's repr() of a float: shortest round trip, ".0" on whole numbers,
// scientific below 1e-4 and from 1e16.
[[nodiscard]] std::string python_float(double value);
// Python's round(value, digits): the exact value rounded half-to-even at
// `digits` decimals, read back as the nearest double.
[[nodiscard]] double python_round(double value, int digits);
// Python 3.12's sum() of floats from the int 0: Neumaier-compensated.
[[nodiscard]] double python_sum(std::initializer_list<double> values);

struct DumpOptions {
  int indent{-1};          // -1: no newlines (Python's indent=None)
  bool ensure_ascii{true}; // escape non-ASCII as \uXXXX
  bool compact{false};     // separators=(",", ":") instead of (", ", ": ")
};

[[nodiscard]] std::string dumps(const Json &value, const DumpOptions &options);

// Writes dumps(value) plus a newline, creating the parent directories.
void write_json(const std::filesystem::path &path, const Json &value, int indent,
                bool ensure_ascii = true);

// json.dumps(value, separators=(",", ":")) plus a newline (the robot files).
void write_compact_json(const std::filesystem::path &path, const Json &value);

// Builders. Integers stay integers and floats stay floats, as in Python.
[[nodiscard]] inline Json null() {
  return Json{nullptr};
}
[[nodiscard]] inline Json boolean(bool value) {
  return Json{value};
}
[[nodiscard]] inline Json real(double value) {
  return Json{value};
}
[[nodiscard]] inline Json text(std::string value) {
  return Json{std::move(value)};
}
template <std::integral T>
  requires(!std::same_as<T, bool>)
[[nodiscard]] Json integer(T value) {
  if constexpr (std::is_signed_v<T>) {
    return Json{static_cast<std::int64_t>(value)};
  } else {
    return Json{static_cast<std::uint64_t>(value)};
  }
}
[[nodiscard]] inline Json array(std::vector<Json> items = {}) {
  return Json{Json::Array(std::move(items))};
}
[[nodiscard]] inline Json object(std::initializer_list<std::pair<std::string, Json>> members = {}) {
  return Json{Json::Object(members)};
}
template <std::integral T> [[nodiscard]] Json optional_integer(const std::optional<T> &value) {
  return value ? integer(*value) : null();
}
template <std::integral T> [[nodiscard]] Json integers(const std::vector<T> &values) {
  Json out = array();
  for (const T value : values) {
    std::get<Json::Array>(out.value).push_back(integer(value));
  }
  return out;
}
[[nodiscard]] Json reals(const std::vector<double> &values);
// round(value, digits) as a float, and a list of them.
[[nodiscard]] inline Json rounded(double value, int digits) {
  return real(python_round(value, digits));
}
[[nodiscard]] Json rounded_list(const std::vector<double> &values, int digits);
[[nodiscard]] Json texts(const std::vector<std::string> &values);

// Appends to an array; sets an object's member as a Python dict does
// (a new key goes last, an existing one keeps its place).
void push(Json &array_value, Json item);
void put(Json &object_value, std::string key, Json item);
// The member `key` of an object (inserted as null when absent).
[[nodiscard]] Json &member(Json &object_value, std::string_view key);
// The member `key` of an object; throws when absent.
[[nodiscard]] const Json &field(const Json &object_value, std::string_view key);

} // namespace phantomcrash::extract
