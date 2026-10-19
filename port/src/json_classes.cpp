#include "phantomcrash/json_classes.hpp"

#include <stdexcept>
#include <string>
#include <variant>

namespace phantomcrash::json {
namespace {

constexpr int deepest_class = 8; // further means a class names itself

JsonValue resolve(const JsonValue &entry, const JsonValue *classes, const int depth) {
  const JsonValue *names = entry.find("class");
  if (names == nullptr) {
    return entry;
  }
  if (depth > deepest_class) {
    throw std::runtime_error("class chain names itself");
  }
  JsonValue out{JsonValue::Object{}};
  const auto add = [&](const JsonValue &name) {
    const JsonValue *found = classes == nullptr ? nullptr : classes->find(name.string());
    if (found == nullptr) {
      throw std::runtime_error("unknown class " + name.string());
    }
    merge(out, resolve(*found, classes, depth + 1));
  };
  if (names->is_array()) {
    for (const auto &name : names->array()) {
      add(name);
    }
  } else {
    add(*names);
  }
  merge(out, entry);
  std::erase_if(std::get<JsonValue::Object>(out.value),
                [](const auto &member) { return member.first == "class"; });
  return out;
}

} // namespace

JsonValue with_classes(const JsonValue &entry, const JsonValue *classes) {
  return resolve(entry, classes, 0);
}

} // namespace phantomcrash::json
