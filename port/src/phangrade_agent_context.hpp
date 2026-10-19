#pragma once
// Source of truth: Phangrade/integration. Keep the port copies identical.
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <cstdlib>
#endif

namespace phangrade {
// What Phangrade knows about this run for a coding agent: where the full logs, crash dumps and
// co-op partners' logs are. Append it to the game's own agent prompt. Phangrade rewrites the
// file while the game runs, so read it when the prompt is copied, not at startup. Empty when
// the game was not launched from Phangrade.
inline std::string agent_context() {
  std::filesystem::path path;
#ifdef _WIN32
  wchar_t value[32768]{};
  const auto length = GetEnvironmentVariableW(L"PHANGRADE_AGENT_CONTEXT", value, 32768);
  if (length == 0 || length >= 32768) {
    return {};
  }
  path = value;
#else
  const char *value = std::getenv("PHANGRADE_AGENT_CONTEXT");
  if (!value || !*value) {
    return {};
  }
  path = value;
#endif
  std::ifstream input(path, std::ios::binary);
  const std::string text{std::istreambuf_iterator<char>(input), {}};
  return text.empty() ? std::string{} : "\nPhangrade context for this run:\n" + text;
}
} // namespace phangrade
