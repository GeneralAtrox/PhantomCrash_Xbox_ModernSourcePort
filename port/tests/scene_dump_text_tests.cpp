#include "test_support.hpp"
#include "../src/desktop_scene_dump_text.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace phantomcrash;

namespace {
void set_agent_context(const char *path) {
#ifdef _WIN32
  _putenv_s("PHANGRADE_AGENT_CONTEXT", path);
#else
  if (*path) {
    setenv("PHANGRADE_AGENT_CONTEXT", path, 1);
  } else {
    unsetenv("PHANGRADE_AGENT_CONTEXT");
  }
#endif
}
} // namespace

TEST_CASE("the copied scene dump is the JSON alone without Phangrade") {
  set_agent_context("");
  const std::string dump = "{\"task\":\"Screen title\",\"problem\":\"\"}";
  CHECK(scene_dump_clipboard(dump) == dump);
}

TEST_CASE("the copied scene dump ends with Phangrade's context for this run") {
  const auto folder = std::filesystem::temp_directory_path() / "phantomcrash-agent-context-test";
  std::filesystem::create_directories(folder);
  const auto context = folder / "phangrade-context.md";
  const std::string body = "Phangrade 0.0.0-test\nLogs: C:\\test\\logs\n";
  {
    std::ofstream(context, std::ios::binary) << body;
  }
  const std::string dump = "{\"task\":\"Screen title\",\"problem\":\"\"}";
  set_agent_context(context.string().c_str());
  CHECK(scene_dump_clipboard(dump) == dump + "\nPhangrade context for this run:\n" + body);
  // A missing file adds nothing.
  std::filesystem::remove_all(folder);
  CHECK(scene_dump_clipboard(dump) == dump);
  set_agent_context("");
}
