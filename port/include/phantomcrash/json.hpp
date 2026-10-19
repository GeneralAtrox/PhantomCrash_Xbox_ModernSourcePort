#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace phantomcrash::json {

// A parsed JSON document. Objects keep their key order.
struct JsonValue {
  using Array = std::vector<JsonValue>;
  using Object = std::vector<std::pair<std::string, JsonValue>>;
  using Storage = std::variant<std::nullptr_t, bool, std::int64_t, std::uint64_t, double,
                               std::string, Array, Object>;

  Storage value{};

  [[nodiscard]] const JsonValue *find(std::string_view key) const noexcept;

  [[nodiscard]] bool is_object() const noexcept {
    return std::holds_alternative<Object>(value);
  }
  [[nodiscard]] bool is_array() const noexcept {
    return std::holds_alternative<Array>(value);
  }
  [[nodiscard]] bool is_string() const noexcept {
    return std::holds_alternative<std::string>(value);
  }
  [[nodiscard]] bool is_number() const noexcept;
  [[nodiscard]] bool is_null() const noexcept {
    return std::holds_alternative<std::nullptr_t>(value);
  }

  // Typed access; each throws std::runtime_error when the type differs.
  [[nodiscard]] double number() const;
  [[nodiscard]] bool boolean() const;
  [[nodiscard]] const std::string &string() const;
  [[nodiscard]] const Array &array() const;
  [[nodiscard]] const Object &object() const;

  // Member access with a fallback when the key is absent. A present member of
  // the wrong type still throws, so typos in values do not pass silently.
  [[nodiscard]] double number_or(std::string_view key, double fallback) const;
  [[nodiscard]] bool boolean_or(std::string_view key, bool fallback) const;
  [[nodiscard]] std::string string_or(std::string_view key, std::string_view fallback) const;
};

[[nodiscard]] JsonValue parse(std::span<const std::uint8_t> bytes);
[[nodiscard]] JsonValue parse(std::string_view text);
[[nodiscard]] JsonValue read_file(const std::filesystem::path &path);

// Mod overlay: members of `overlay` replace those of `base`, and objects on
// both sides merge recursively. Arrays and scalars are replaced whole.
void merge(JsonValue &base, const JsonValue &overlay);

// Pretty-printed JSON with two-space indentation and a trailing newline.
[[nodiscard]] std::string write(const JsonValue &value);

} // namespace phantomcrash::json
