#pragma once

// Minimal self-registering test harness for the core library.

#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace phantomcrash::test {

struct Case {
  const char *name;
  std::function<void()> body;
};

inline std::vector<Case> &cases() {
  static std::vector<Case> registry;
  return registry;
}

struct Register {
  Register(const char *name, std::function<void()> body) {
    cases().push_back({name, std::move(body)});
  }
};

struct Failure : std::runtime_error {
  using std::runtime_error::runtime_error;
};

[[noreturn]] inline void fail(const char *file, int line, const std::string &what) {
  std::ostringstream message;
  message << file << ":" << line << ": " << what;
  throw Failure(message.str());
}

} // namespace phantomcrash::test

#define PC_CONCAT_INNER(a, b) a##b
#define PC_CONCAT(a, b) PC_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                                            \
  static void PC_CONCAT(test_body_, __LINE__)();                                                   \
  static const ::phantomcrash::test::Register PC_CONCAT(test_register_, __LINE__)(                 \
      name, &PC_CONCAT(test_body_, __LINE__));                                                     \
  static void PC_CONCAT(test_body_, __LINE__)()

#define CHECK(condition)                                                                           \
  do {                                                                                             \
    if (!(condition)) {                                                                            \
      ::phantomcrash::test::fail(__FILE__, __LINE__, "CHECK(" #condition ") failed");              \
    }                                                                                              \
  } while (false)

// A check the rest of the case depends on: it stops there when it fails.
#define REQUIRE(condition)                                                                         \
  do {                                                                                             \
    CHECK(condition);                                                                              \
    if (!(condition)) {                                                                            \
      return;                                                                                      \
    }                                                                                              \
  } while (false)

#define CHECK_THROWS(expression)                                                                   \
  do {                                                                                             \
    bool thrown = false;                                                                           \
    try {                                                                                          \
      static_cast<void>(expression);                                                               \
    } catch (const std::exception &) {                                                             \
      thrown = true;                                                                               \
    }                                                                                              \
    if (!thrown) {                                                                                 \
      ::phantomcrash::test::fail(__FILE__, __LINE__, "expected a throw: " #expression);            \
    }                                                                                              \
  } while (false)
