#include "test_support.hpp"

#include <cstdio>
#include <exception>

#include "phantomcrash/version.hpp"

TEST_CASE("port identity") {
  CHECK(!phantomcrash::port_name().empty());
  CHECK(!phantomcrash::port_version().empty());
}

int main() {
  int failed = 0;
  for (const auto &test : phantomcrash::test::cases()) {
    try {
      test.body();
      std::printf("PASS: %s\n", test.name);
    } catch (const std::exception &error) {
      ++failed;
      std::printf("FAIL: %s\n  %s\n", test.name, error.what());
    }
  }
  std::printf("%zu tests, %d failed\n", phantomcrash::test::cases().size(), failed);
  return failed == 0 ? 0 : 1;
}
