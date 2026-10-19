#pragma once
#include "state_writer.hpp"
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

namespace phantomcrash::online {
// Named short digests and readable values of the parts of a confirmed state.
// Peers log these when their states differ; comparing the two logs line by
// line shows which parts went apart. Each part is digested on its own, so the
// values only compare with another peer's sections, not with state_digest().
class StateSections {
public:
  void begin(std::string label) {
    end();
    line_ = std::move(label);
  }
  template <class... T> void add(const char *name, const T &...values) {
    StateWriter out;
    out(values...);
    const auto digest = out.finish();
    char hex[9];
    std::snprintf(hex, sizeof(hex), "%02x%02x%02x%02x", digest[0], digest[1], digest[2], digest[3]);
    append(name, hex);
  }
  template <class T> void show(const char *name, const T &value) {
    append(name, readable(value));
  }
  void end() {
    if (!line_.empty()) {
      lines_.push_back(std::move(line_));
      line_.clear();
    }
  }
  [[nodiscard]] std::vector<std::string> take() {
    end();
    return std::move(lines_);
  }

private:
  void append(const char *name, const std::string &value) {
    line_ += ' ';
    line_ += name;
    line_ += '=';
    line_ += value;
  }
  template <class T> static std::string readable(const T &value) {
    if constexpr (std::is_same_v<T, bool>) {
      return value ? "1" : "0";
    } else if constexpr (std::is_floating_point_v<T>) {
      char text[32];
      std::snprintf(text, sizeof(text), "%.6g", double(value));
      return text;
    } else if constexpr (std::is_enum_v<T>) {
      return std::to_string(static_cast<long long>(value));
    } else if constexpr (std::is_integral_v<T>) {
      return std::to_string(value);
    } else {
      return readable(value.x) + "," + readable(value.y) + "," + readable(value.z);
    }
  }
  std::string line_;
  std::vector<std::string> lines_;
};
} // namespace phantomcrash::online
