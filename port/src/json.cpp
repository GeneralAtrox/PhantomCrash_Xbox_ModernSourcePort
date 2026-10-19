// Strict JSON parser (from the Record of Lodoss War port): rejects comments,
// trailing commas, duplicate keys and leading zeros so data mistakes surface.
#include "phantomcrash/json.hpp"

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace phantomcrash::json {
namespace {

class Parser final {
public:
  explicit Parser(const std::span<const std::uint8_t> bytes)
      : text_(reinterpret_cast<const char *>(bytes.data()), bytes.size()) {}

  JsonValue parse() {
    whitespace();
    auto result = value(0);
    whitespace();
    if (cursor_ != text_.size()) {
      fail("trailing data");
    }
    return result;
  }

private:
  [[noreturn]] void fail(const std::string_view reason) const {
    throw std::runtime_error("JSON " + std::string(reason) + " at byte " + std::to_string(cursor_));
  }

  void whitespace() {
    while (cursor_ < text_.size() && (text_[cursor_] == ' ' || text_[cursor_] == '\t' ||
                                      text_[cursor_] == '\r' || text_[cursor_] == '\n')) {
      ++cursor_;
    }
  }

  bool take(const char expected) {
    if (cursor_ < text_.size() && text_[cursor_] == expected) {
      ++cursor_;
      return true;
    }
    return false;
  }

  void literal(const std::string_view expected) {
    if (text_.substr(cursor_, expected.size()) != expected) {
      fail("literal differs");
    }
    cursor_ += expected.size();
  }

  JsonValue value(const std::size_t depth) {
    if (depth > 256 || cursor_ >= text_.size()) {
      fail("value is absent or too deep");
    }
    switch (text_[cursor_]) {
    case 'n':
      literal("null");
      return JsonValue{nullptr};
    case 't':
      literal("true");
      return JsonValue{true};
    case 'f':
      literal("false");
      return JsonValue{false};
    case '"':
      return JsonValue{string()};
    case '[':
      return array(depth + 1);
    case '{':
      return object(depth + 1);
    default:
      return number();
    }
  }

  static void utf8(std::string &output, const std::uint32_t point) {
    if (point <= 0x7f) {
      output.push_back(static_cast<char>(point));
    } else if (point <= 0x7ff) {
      output.push_back(static_cast<char>(0xc0 | (point >> 6)));
      output.push_back(static_cast<char>(0x80 | (point & 0x3f)));
    } else if (point <= 0xffff) {
      output.push_back(static_cast<char>(0xe0 | (point >> 12)));
      output.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (point & 0x3f)));
    } else {
      output.push_back(static_cast<char>(0xf0 | (point >> 18)));
      output.push_back(static_cast<char>(0x80 | ((point >> 12) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (point & 0x3f)));
    }
  }

  std::uint32_t hex4() {
    if (text_.size() - cursor_ < 4) {
      fail("unicode escape is truncated");
    }
    std::uint32_t result{};
    for (int index = 0; index < 4; ++index) {
      const char character = text_[cursor_++];
      result <<= 4;
      if (character >= '0' && character <= '9') {
        result |= character - '0';
      } else if (character >= 'a' && character <= 'f') {
        result |= character - 'a' + 10;
      } else if (character >= 'A' && character <= 'F') {
        result |= character - 'A' + 10;
      } else {
        fail("unicode escape differs");
      }
    }
    return result;
  }

  std::string string() {
    if (!take('"')) {
      fail("string is absent");
    }
    std::string output;
    while (cursor_ < text_.size()) {
      const auto character = static_cast<unsigned char>(text_[cursor_++]);
      if (character == '"') {
        return output;
      }
      if (character < 0x20) {
        fail("string contains a control byte");
      }
      if (character != '\\') {
        output.push_back(static_cast<char>(character));
        continue;
      }
      if (cursor_ >= text_.size()) {
        fail("string escape is truncated");
      }
      switch (text_[cursor_++]) {
      case '"':
        output.push_back('"');
        break;
      case '\\':
        output.push_back('\\');
        break;
      case '/':
        output.push_back('/');
        break;
      case 'b':
        output.push_back('\b');
        break;
      case 'f':
        output.push_back('\f');
        break;
      case 'n':
        output.push_back('\n');
        break;
      case 'r':
        output.push_back('\r');
        break;
      case 't':
        output.push_back('\t');
        break;
      case 'u': {
        auto point = hex4();
        if (point >= 0xd800 && point <= 0xdbff) {
          if (text_.size() - cursor_ < 2 || text_[cursor_] != '\\' || text_[cursor_ + 1] != 'u') {
            fail("high surrogate lacks a low surrogate");
          }
          cursor_ += 2;
          const auto low = hex4();
          if (low < 0xdc00 || low > 0xdfff) {
            fail("low surrogate differs");
          }
          point = 0x10000 + ((point - 0xd800) << 10) + (low - 0xdc00);
        } else if (point >= 0xdc00 && point <= 0xdfff) {
          fail("unpaired low surrogate");
        }
        utf8(output, point);
        break;
      }
      default:
        fail("string escape differs");
      }
    }
    fail("string is unterminated");
  }

  JsonValue number() {
    const auto start = cursor_;
    take('-');
    if (cursor_ >= text_.size()) {
      fail("number is truncated");
    }
    if (take('0')) {
      if (cursor_ < text_.size() && text_[cursor_] >= '0' && text_[cursor_] <= '9') {
        fail("number has a leading zero");
      }
    } else {
      if (text_[cursor_] < '1' || text_[cursor_] > '9') {
        fail("number is absent");
      }
      while (cursor_ < text_.size() && text_[cursor_] >= '0' && text_[cursor_] <= '9') {
        ++cursor_;
      }
    }
    bool floating = false;
    if (take('.')) {
      floating = true;
      const auto digits = cursor_;
      while (cursor_ < text_.size() && text_[cursor_] >= '0' && text_[cursor_] <= '9') {
        ++cursor_;
      }
      if (cursor_ == digits) {
        fail("fraction is empty");
      }
    }
    if (cursor_ < text_.size() && (text_[cursor_] == 'e' || text_[cursor_] == 'E')) {
      floating = true;
      ++cursor_;
      if (cursor_ < text_.size() && (text_[cursor_] == '+' || text_[cursor_] == '-')) {
        ++cursor_;
      }
      const auto digits = cursor_;
      while (cursor_ < text_.size() && text_[cursor_] >= '0' && text_[cursor_] <= '9') {
        ++cursor_;
      }
      if (cursor_ == digits) {
        fail("exponent is empty");
      }
    }
    const auto token = text_.substr(start, cursor_ - start);
    if (floating) {
      double parsed{};
      const auto result = std::from_chars(token.data(), token.data() + token.size(), parsed);
      if (result.ec != std::errc{} || result.ptr != token.data() + token.size() ||
          !std::isfinite(parsed)) {
        fail("floating number differs");
      }
      return JsonValue{parsed};
    }
    if (!token.empty() && token.front() == '-') {
      std::int64_t parsed{};
      const auto result = std::from_chars(token.data(), token.data() + token.size(), parsed);
      if (result.ec != std::errc{}) {
        fail("signed integer is outside range");
      }
      return JsonValue{parsed};
    }
    std::uint64_t parsed{};
    const auto result = std::from_chars(token.data(), token.data() + token.size(), parsed);
    if (result.ec != std::errc{}) {
      fail("unsigned integer is outside range");
    }
    return JsonValue{parsed};
  }

  JsonValue array(const std::size_t depth) {
    take('[');
    whitespace();
    JsonValue::Array result;
    if (take(']')) {
      return JsonValue{std::move(result)};
    }
    while (true) {
      result.push_back(value(depth));
      whitespace();
      if (take(']')) {
        return JsonValue{std::move(result)};
      }
      if (!take(',')) {
        fail("array separator differs");
      }
      whitespace();
    }
  }

  JsonValue object(const std::size_t depth) {
    take('{');
    whitespace();
    JsonValue::Object result;
    if (take('}')) {
      return JsonValue{std::move(result)};
    }
    while (true) {
      if (cursor_ >= text_.size() || text_[cursor_] != '"') {
        fail("object key is absent");
      }
      auto key = string();
      for (const auto &[existing, ignored] : result) {
        static_cast<void>(ignored);
        if (existing == key) {
          fail("object key is duplicated");
        }
      }
      whitespace();
      if (!take(':')) {
        fail("object colon differs");
      }
      whitespace();
      result.emplace_back(std::move(key), value(depth));
      whitespace();
      if (take('}')) {
        return JsonValue{std::move(result)};
      }
      if (!take(',')) {
        fail("object separator differs");
      }
      whitespace();
    }
  }

  std::string_view text_;
  std::size_t cursor_{};
};
} // namespace

JsonValue parse(const std::span<const std::uint8_t> bytes) {
  return Parser(bytes).parse();
}

} // namespace phantomcrash::json
