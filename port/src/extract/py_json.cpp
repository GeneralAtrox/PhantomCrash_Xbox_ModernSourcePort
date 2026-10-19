#include "py_json.hpp"

#include "binary.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <variant>

namespace phantomcrash::extract {
namespace {

void append_hex4(std::string &out, unsigned value) {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "\\u%04x", value);
  out += buffer;
}

// One UTF-8 sequence starting at `index`; advances it.
char32_t decode_utf8(std::string_view text, std::size_t &index) {
  const auto byte = [&](std::size_t at) { return static_cast<unsigned char>(text.at(at)); };
  const unsigned lead = byte(index);
  if (lead < 0x80) {
    ++index;
    return lead;
  }
  const int extra = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : 1;
  char32_t code = lead & (0x3F >> extra);
  for (int i = 1; i <= extra; ++i) {
    code = (code << 6) | (byte(index + static_cast<std::size_t>(i)) & 0x3F);
  }
  index += static_cast<std::size_t>(extra) + 1;
  return code;
}

void write_string(std::string &out, std::string_view value, bool ensure_ascii) {
  out += '"';
  std::size_t index = 0;
  while (index < value.size()) {
    const auto c = static_cast<unsigned char>(value[index]);
    if (c >= 0x80) {
      const std::size_t start = index;
      const char32_t code = decode_utf8(value, index);
      if (!ensure_ascii) {
        out.append(value.substr(start, index - start));
      } else if (code >= 0x10000) {
        const char32_t v = code - 0x10000;
        append_hex4(out, 0xD800 | static_cast<unsigned>(v >> 10));
        append_hex4(out, 0xDC00 | static_cast<unsigned>(v & 0x3FF));
      } else {
        append_hex4(out, static_cast<unsigned>(code));
      }
      continue;
    }
    ++index;
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    default:
      if (c < 0x20) {
        append_hex4(out, c);
      } else {
        out += static_cast<char>(c);
      }
    }
  }
  out += '"';
}

class Writer {
public:
  explicit Writer(const DumpOptions &options) : options_(options) {}

  void value(const Json &v, int level) {
    std::visit([&](const auto &x) { write(x, level); }, v.value);
  }

  std::string out;

private:
  void write(std::nullptr_t, int) {
    out += "null";
  }
  void write(bool v, int) {
    out += v ? "true" : "false";
  }
  void write(std::int64_t v, int) {
    out += std::to_string(v);
  }
  void write(std::uint64_t v, int) {
    out += std::to_string(v);
  }
  void write(double v, int) {
    out += python_float(v);
  }
  void write(const std::string &v, int) {
    write_string(out, v, options_.ensure_ascii);
  }
  void write(const Json::Array &items, int level) {
    if (items.empty()) {
      out += "[]";
      return;
    }
    out += '[';
    for (std::size_t i = 0; i < items.size(); ++i) {
      separator(i, level + 1);
      value(items[i], level + 1);
    }
    close(level);
    out += ']';
  }
  void write(const Json::Object &members, int level) {
    if (members.empty()) {
      out += "{}";
      return;
    }
    out += '{';
    for (std::size_t i = 0; i < members.size(); ++i) {
      separator(i, level + 1);
      write_string(out, members[i].first, options_.ensure_ascii);
      out += options_.compact ? ":" : ": ";
      value(members[i].second, level + 1);
    }
    close(level);
    out += '}';
  }
  void separator(std::size_t index, int level) {
    if (index > 0) {
      out += options_.indent >= 0 || options_.compact ? "," : ", ";
    }
    if (options_.indent >= 0) {
      out += '\n';
      out.append(static_cast<std::size_t>(options_.indent * level), ' ');
    }
  }
  void close(int level) {
    if (options_.indent >= 0) {
      out += '\n';
      out.append(static_cast<std::size_t>(options_.indent * level), ' ');
    }
  }

  const DumpOptions &options_;
};

} // namespace

const Json &field(const Json &object_value, const std::string_view key) {
  const Json *found = object_value.find(key);
  if (found == nullptr) {
    throw FormatError("Missing member " + std::string(key));
  }
  return *found;
}

double python_round(const double value, const int digits) {
  if (!std::isfinite(value)) {
    return value;
  }
  // to_chars with a precision converts the exact binary value and breaks
  // exact ties to even, as CPython's dtoa does.
  char buffer[400];
  const auto written = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                     std::chars_format::fixed, digits);
  double out = 0;
  std::from_chars(buffer, written.ptr, out);
  return out;
}

double python_sum(const std::initializer_list<double> values) {
  if (values.size() == 0) {
    return 0.0;
  }
  auto item = values.begin();
  double total = 0.0 + *item; // int 0 + float, as sum() starts
  double compensation = 0.0;
  for (++item; item != values.end(); ++item) {
    const double x = *item;
    const double t = total + x;
    if (std::fabs(total) >= std::fabs(x)) {
      compensation += (total - t) + x;
    } else {
      compensation += (x - t) + total;
    }
    total = t;
  }
  if (compensation != 0.0 && std::isfinite(compensation)) {
    total += compensation;
  }
  return total;
}

std::string python_float(const double value) {
  if (std::isnan(value)) {
    return "NaN";
  }
  if (std::isinf(value)) {
    return value < 0 ? "-Infinity" : "Infinity";
  }
  if (value == 0) {
    return std::signbit(value) ? "-0.0" : "0.0";
  }
  char buffer[64];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                    std::chars_format::scientific);
  const std::string_view sci(buffer, static_cast<std::size_t>(result.ptr - buffer));
  const std::size_t e = sci.find('e');
  std::string digits;
  bool negative = false;
  for (const char c : sci.substr(0, e)) {
    if (c == '-') {
      negative = true;
    } else if (c != '.') {
      digits += c;
    }
  }
  int exponent = 0;
  std::from_chars(sci.data() + e + 1 + (sci[e + 1] == '+' ? 1 : 0), sci.data() + sci.size(),
                  exponent);
  std::string out = negative ? "-" : "";
  const int count = static_cast<int>(digits.size());
  if (exponent < -4 || exponent >= 16) {
    out += digits[0];
    if (count > 1) {
      out += '.';
      out += digits.substr(1);
    }
    char tail[16];
    std::snprintf(tail, sizeof(tail), "e%c%02d", exponent < 0 ? '-' : '+', std::abs(exponent));
    return out + tail;
  }
  const int point = exponent + 1; // digits before the decimal point
  if (point <= 0) {
    out += "0.";
    out.append(static_cast<std::size_t>(-point), '0');
    out += digits;
  } else if (point >= count) {
    out += digits;
    out.append(static_cast<std::size_t>(point - count), '0');
    out += ".0";
  } else {
    out += digits.substr(0, static_cast<std::size_t>(point));
    out += '.';
    out += digits.substr(static_cast<std::size_t>(point));
  }
  return out;
}

std::string dumps(const Json &value, const DumpOptions &options) {
  Writer writer(options);
  writer.value(value, 0);
  return std::move(writer.out);
}

void write_json(const std::filesystem::path &path, const Json &value, const int indent,
                const bool ensure_ascii) {
  write_text(path, dumps(value, {indent, ensure_ascii, false}) + "\n");
}

void write_compact_json(const std::filesystem::path &path, const Json &value) {
  write_text(path, dumps(value, {-1, true, true}) + "\n");
}

Json rounded_list(const std::vector<double> &values, const int digits) {
  Json out = array();
  for (const double v : values) {
    push(out, rounded(v, digits));
  }
  return out;
}

Json reals(const std::vector<double> &values) {
  Json out = array();
  for (const double v : values) {
    push(out, real(v));
  }
  return out;
}

Json texts(const std::vector<std::string> &values) {
  Json out = array();
  for (const auto &v : values) {
    push(out, text(v));
  }
  return out;
}

void push(Json &array_value, Json item) {
  std::get<Json::Array>(array_value.value).push_back(std::move(item));
}

void put(Json &object_value, std::string key, Json item) {
  member(object_value, key) = std::move(item);
}

Json &member(Json &object_value, std::string_view key) {
  auto &members = std::get<Json::Object>(object_value.value);
  for (auto &[name, item] : members) {
    if (name == key) {
      return item;
    }
  }
  members.emplace_back(std::string(key), null());
  return members.back().second;
}

} // namespace phantomcrash::extract
