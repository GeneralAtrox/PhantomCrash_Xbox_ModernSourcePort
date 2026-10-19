#include "phantomcrash/version.hpp"

namespace phantomcrash {

std::string_view port_name() {
  return "Phantom Crash";
}

std::string_view port_version() {
  return PHANTOMCRASH_VERSION; // vcpkg.json's version-string, set by CMake
}

} // namespace phantomcrash
