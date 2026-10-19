// Typed access, file loading, mod overlay merging and writing for JsonValue.
#include "phantomcrash/json.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace phantomcrash::json {
namespace {

[[noreturn]] void wrong_type(const char *expected) {
  throw std::runtime_error(std::string("JSON value is not ") + expected);
}

void write_string(std::string &output, const std::string &text) {
  output.push_back('"');
  for (const char character : text) {
    switch (character) {
    case '"':
      output += "\\\"";
      break;
    case '\\':
      output += "\\\\";
      break;
    case '\n':
      output += "\\n";
      break;
    case '\r':
      output += "\\r";
      break;
    case '\t':
      output += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(character) < 0x20) {
        constexpr char digits[] = "0123456789abcdef";
        output += "\\u00";
        output.push_back(digits[(character >> 4) & 0xf]);
        output.push_back(digits[character & 0xf]);
      } else {
        output.push_back(character);
      }
    }
  }
  output.push_back('"');
}

void write_value(std::string &output, const JsonValue &value, int depth) {
  const auto newline = [&](int level) {
    output.push_back('\n');
    output.append(static_cast<std::size_t>(level) * 2, ' ');
  };
  if (std::holds_alternative<std::nullptr_t>(value.value)) {
    output += "null";
  } else if (const auto *flag = std::get_if<bool>(&value.value)) {
    output += *flag ? "true" : "false";
  } else if (const auto *signed_value = std::get_if<std::int64_t>(&value.value)) {
    output += std::to_string(*signed_value);
  } else if (const auto *unsigned_value = std::get_if<std::uint64_t>(&value.value)) {
    output += std::to_string(*unsigned_value);
  } else if (const auto *floating = std::get_if<double>(&value.value)) {
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), *floating);
    std::string_view text(buffer, static_cast<std::size_t>(result.ptr - buffer));
    output += text;
    // Keep the value a float when read back.
    if (text.find_first_of(".eE") == std::string_view::npos) {
      output += ".0";
    }
  } else if (const auto *text = std::get_if<std::string>(&value.value)) {
    write_string(output, *text);
  } else if (const auto *array = std::get_if<JsonValue::Array>(&value.value)) {
    output.push_back('[');
    for (std::size_t index = 0; index < array->size(); ++index) {
      output += index == 0 ? "" : ",";
      newline(depth + 1);
      write_value(output, (*array)[index], depth + 1);
    }
    if (!array->empty()) {
      newline(depth);
    }
    output.push_back(']');
  } else {
    const auto &object = std::get<JsonValue::Object>(value.value);
    output.push_back('{');
    for (std::size_t index = 0; index < object.size(); ++index) {
      output += index == 0 ? "" : ",";
      newline(depth + 1);
      write_string(output, object[index].first);
      output += ": ";
      write_value(output, object[index].second, depth + 1);
    }
    if (!object.empty()) {
      newline(depth);
    }
    output.push_back('}');
  }
}

} // namespace

const JsonValue *JsonValue::find(const std::string_view key) const noexcept {
  const auto *members = std::get_if<Object>(&value);
  if (members == nullptr) {
    return nullptr;
  }
  for (const auto &[candidate, child] : *members) {
    if (candidate == key) {
      return &child;
    }
  }
  return nullptr;
}

bool JsonValue::is_number() const noexcept {
  return std::holds_alternative<double>(value) || std::holds_alternative<std::int64_t>(value) ||
         std::holds_alternative<std::uint64_t>(value);
}

double JsonValue::number() const {
  if (const auto *floating = std::get_if<double>(&value)) {
    return *floating;
  }
  if (const auto *signed_value = std::get_if<std::int64_t>(&value)) {
    return static_cast<double>(*signed_value);
  }
  if (const auto *unsigned_value = std::get_if<std::uint64_t>(&value)) {
    return static_cast<double>(*unsigned_value);
  }
  wrong_type("a number");
}

bool JsonValue::boolean() const {
  if (const auto *flag = std::get_if<bool>(&value)) {
    return *flag;
  }
  wrong_type("a boolean");
}

const std::string &JsonValue::string() const {
  if (const auto *text = std::get_if<std::string>(&value)) {
    return *text;
  }
  wrong_type("a string");
}

const JsonValue::Array &JsonValue::array() const {
  if (const auto *items = std::get_if<Array>(&value)) {
    return *items;
  }
  wrong_type("an array");
}

const JsonValue::Object &JsonValue::object() const {
  if (const auto *members = std::get_if<Object>(&value)) {
    return *members;
  }
  wrong_type("an object");
}

double JsonValue::number_or(const std::string_view key, const double fallback) const {
  const auto *member = find(key);
  return member == nullptr ? fallback : member->number();
}

bool JsonValue::boolean_or(const std::string_view key, const bool fallback) const {
  const auto *member = find(key);
  return member == nullptr ? fallback : member->boolean();
}

std::string JsonValue::string_or(const std::string_view key,
                                 const std::string_view fallback) const {
  const auto *member = find(key);
  return member == nullptr ? std::string(fallback) : member->string();
}

JsonValue parse(const std::string_view text) {
  return parse(std::span(reinterpret_cast<const std::uint8_t *>(text.data()), text.size()));
}

JsonValue read_file(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("Unable to open " + path.string());
  }
  const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
  try {
    return parse(text);
  } catch (const std::exception &error) {
    throw std::runtime_error(path.string() + ": " + error.what());
  }
}

void merge(JsonValue &base, const JsonValue &overlay) {
  auto *base_members = std::get_if<JsonValue::Object>(&base.value);
  const auto *overlay_members = std::get_if<JsonValue::Object>(&overlay.value);
  if (base_members == nullptr || overlay_members == nullptr) {
    base = overlay;
    return;
  }
  for (const auto &[key, child] : *overlay_members) {
    auto existing = std::find_if(base_members->begin(), base_members->end(),
                                 [&](const auto &member) { return member.first == key; });
    if (existing == base_members->end()) {
      base_members->emplace_back(key, child);
    } else {
      merge(existing->second, child);
    }
  }
}

std::string write(const JsonValue &value) {
  std::string output;
  write_value(output, value, 0);
  output.push_back('\n');
  return output;
}

} // namespace phantomcrash::json
