#include "test_support.hpp"
#include "phantomcrash/online_content.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace phantomcrash::online;
namespace fs = std::filesystem;

TEST_CASE("SLAI toggle and active pack content participate in multiplayer compatibility") {
  const auto root = fs::temp_directory_path() /
                    ("phantom-slai-hash-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  struct Cleanup {
    fs::path root;
    ~Cleanup() {
      fs::remove_all(root);
    }
  } cleanup{root};
  const auto write = [&](const fs::path &relative, const std::string &text) {
    fs::create_directories((root / relative).parent_path());
    std::ofstream(root / relative) << text;
  };
  write("game.exe", "game");
  write("data/rules.json", "{}");
  write("artifacts/europe/assets/base.txt", "base");
  const auto off = content_fingerprint(root / "game.exe", root, false);
  write("artifacts/slai/assets/installed.json", R"({"version":1,"disc":"SLES_529.40"})");
  for (auto file : {"tables/parts.json", "tables/garage.json", "robots/catalogue.json",
                    "slai/native_parts.json", "slai/assets/manifest.json"}) {
    write(fs::path("artifacts/slai/assets") / file, "{}");
  }
  CHECK(content_fingerprint(root / "game.exe", root, false) == off);
  const auto on = content_fingerprint(root / "game.exe", root, true);
  CHECK(on != off);
  write("artifacts/slai/assets/tables/parts.json", "{\"changed\":true}");
  CHECK(content_fingerprint(root / "game.exe", root, true) != on);
  CHECK(content_fingerprint(root / "game.exe", root, false) == off);
}

namespace {
struct ContentFixture {
  fs::path root = fs::temp_directory_path() /
                  ("phantomcrash-online-content-" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  ContentFixture() {
    fs::create_directory(root);
  }
  ~ContentFixture() {
    std::error_code error;
    fs::remove_all(root, error);
  }
  void write(const fs::path &path, const std::string &bytes) {
    fs::create_directories((root / path).parent_path());
    std::ofstream(root / path, std::ios::binary) << bytes;
  }
  void populate(const std::string &folder) {
    write(folder + "/game.exe", "executable");
    write(folder + "/data/rules.json", "rules");
    write(folder + "/artifacts/europe/assets/robot.gltf", R"({"asset":{"version":"2.0"}})");
  }
  Compatibility fingerprint(const std::string &folder) {
    return content_fingerprint(root / folder / "game.exe", root / folder);
  }
};
} // namespace

TEST_CASE("online compatibility covers executable and content independently of install path") {
  ContentFixture files;
  files.populate("host");
  files.populate("client");
  const auto original = files.fingerprint("host");
  CHECK(original == files.fingerprint("client"));
  files.write("client/settings.json", "independent profile");
  CHECK(original == files.fingerprint("client"));
  // The converter records its own absolute output folder in the manifest.
  files.write("host/artifacts/europe/assets/manifest.json", R"({"source":"C:\Host"})");
  files.write("client/artifacts/europe/assets/manifest.json", R"({"source":"D:\Other\Client"})");
  CHECK(files.fingerprint("host") == files.fingerprint("client"));
  CHECK(original == files.fingerprint("client"));
  files.write("client/data/rules.json", "Rules"); // same size still changes the digest
  CHECK(original != files.fingerprint("client"));
  files.populate("client");
  files.write("client/artifacts/europe/assets/robot.gltf",
              R"({"asset":{"version":"2.0"},"extras":"changed"})");
  CHECK(original != files.fingerprint("client"));
  files.populate("client");
  files.write("client/game.exe", "Executable");
  CHECK(original != files.fingerprint("client"));
  files.populate("client");
  files.write("client/mods/data/rules.json", "mod rules");
  CHECK(original != files.fingerprint("client"));
  CHECK_THROWS(files.fingerprint("missing"));
  const auto root = files.root;
  fs::remove_all(root);
  CHECK(!fs::exists(root));
}

TEST_CASE("online compatibility includes raw fallbacks and decoded external model buffers") {
  ContentFixture files;
  files.populate("host");
  files.populate("client");
  const auto base = files.fingerprint("host");
  files.write("client/artifacts/europe/files/Media/table.bin", "raw table");
  CHECK(files.fingerprint("client") != base);
  files.write("host/artifacts/europe/files/Media/table.bin", "raw table");
  CHECK(files.fingerprint("host") == files.fingerprint("client"));
  for (const auto *side : {"host", "client"}) {
    const std::string folder = side;
    files.write(folder + "/payload data.bin", "mesh");
    files.write(folder + "/artifacts/europe/assets/robot.gltf", R"({
      "asset":{"version":"2.0"},
      "buffers":[{"uri":"../../../payload%20data.bin","byteLength":4}]
    })");
  }
  const auto reference = files.fingerprint("host");
  CHECK(reference == files.fingerprint("client"));
  files.write("client/payload data.bin", "Mesh");
  CHECK(reference != files.fingerprint("client"));
  fs::remove(files.root / "client/payload data.bin");
  CHECK_THROWS(files.fingerprint("client"));
}
