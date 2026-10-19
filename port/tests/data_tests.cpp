// JSON parsing, mod overlay merging and data path resolution.

#include "test_support.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/game_text.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/slai_content.hpp"

using phantomcrash::DataRoots;
namespace json = phantomcrash::json;
namespace fs = std::filesystem;

namespace {

// A fresh folder under the system temp directory, removed afterwards.
class TempFolder final {
public:
  TempFolder() {
    static std::atomic<int> counter{};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = fs::temp_directory_path() /
            ("phantomcrash-test-" + std::to_string(stamp) + "-" + std::to_string(counter++));
    fs::create_directories(path_);
  }
  ~TempFolder() {
    std::error_code error;
    fs::remove_all(path_, error);
  }
  TempFolder(const TempFolder &) = delete;
  TempFolder &operator=(const TempFolder &) = delete;

  [[nodiscard]] const fs::path &path() const {
    return path_;
  }
  void write(const fs::path &relative, const std::string &text) const {
    fs::create_directories((path_ / relative).parent_path());
    std::ofstream(path_ / relative, std::ios::binary) << text;
  }

private:
  fs::path path_;
};

} // namespace

TEST_CASE("json parses numbers, strings and nesting") {
  const auto value = json::parse(R"({"a": 1, "b": -2.5, "c": "xé", "d": [true, null]})");
  CHECK(value.find("a")->number() == 1.0);
  CHECK(value.find("b")->number() == -2.5);
  CHECK(value.find("c")->string() == "x\xc3\xa9");
  CHECK(value.find("d")->array().size() == 2);
  CHECK(value.find("d")->array()[0].boolean());
  CHECK(value.find("missing") == nullptr);
  CHECK(value.number_or("missing", 7.0) == 7.0);
  CHECK(value.string_or("c", "") == "x\xc3\xa9");
}

TEST_CASE("json rejects mistakes instead of guessing") {
  CHECK_THROWS(json::parse(R"({"a": 1,})"));
  CHECK_THROWS(json::parse(R"({"a": 1, "a": 2})"));
  CHECK_THROWS(json::parse(R"({"a": 01})"));
  CHECK_THROWS(json::parse("// comment\n{}"));
  CHECK_THROWS(json::parse(R"({"a": "text"})").number_or("a", 0.0));
}

TEST_CASE("json write round-trips") {
  const auto original =
      json::parse(R"({"s": "q\"\\\n", "f": 0.5, "whole": 2.0, "i": -3, "a": [], "o": {}})");
  const auto again = json::parse(json::write(original));
  CHECK(again.find("s")->string() == "q\"\\\n");
  CHECK(again.find("f")->number() == 0.5);
  CHECK(std::holds_alternative<double>(again.find("whole")->value));
  CHECK(again.find("i")->number() == -3.0);
  CHECK(again.find("a")->array().empty());
  CHECK(again.find("o")->object().empty());
}

TEST_CASE("json merge overlays objects and replaces the rest") {
  auto base = json::parse(R"({"keep": 1, "nested": {"x": 1, "y": 2}, "list": [1, 2, 3]})");
  json::merge(base, json::parse(R"({"nested": {"y": 5, "z": 6}, "list": [9], "new": true})"));
  CHECK(base.find("keep")->number() == 1.0);
  CHECK(base.find("nested")->find("x")->number() == 1.0);
  CHECK(base.find("nested")->find("y")->number() == 5.0);
  CHECK(base.find("nested")->find("z")->number() == 6.0);
  CHECK(base.find("list")->array().size() == 1);
  CHECK(base.find("new")->boolean());
}

TEST_CASE("data documents merge the mod overlay") {
  TempFolder root;
  root.write("data/ui/title.json", R"({"music": "title", "idle": {"seconds": 30.0}})");
  root.write("mods/data/ui/title.json", R"({"idle": {"seconds": 5.0}})");
  const auto roots = DataRoots::standard(root.path());
  const auto document = roots.document("ui/title.json");
  CHECK(document.find("music")->string() == "title");
  CHECK(document.find("idle")->number_or("seconds", 0) == 5.0);
  CHECK_THROWS(roots.document("ui/missing.json"));
  CHECK_THROWS(roots.document("../outside.json"));
}

TEST_CASE("data documents take the game's text from the converted table") {
  TempFolder root;
  root.write("data/ui/help.json",
             R"({"help": "{xbe:0x19b848}", "{xbe:0x1}": ["Role — {xbe:0x0001a2}",
    "{xbe:nothing} stays", 3]})");
  root.write("artifacts/europe/assets/tables/text.json",
             R"({"strings": {"0x19b848": "Set the vibration.", "0x0001a2": "A square."}})");
  const auto document = DataRoots::standard(root.path()).document("ui/help.json");
  CHECK(document.find("help")->string() == "Set the vibration.");
  const auto &list = document.find("{xbe:0x1}")->array(); // keys stay as written
  CHECK(list[0].string() == "Role — A square.");
  CHECK(list[1].string() == "{xbe:nothing} stays");

  // A mod's table replaces the converted one, as for any asset.
  root.write("mods/tables/text.json",
             R"({"strings": {"0x19b848": "Vibration einstellen.", "0x1a2": "Ein Quadrat."}})");
  CHECK(DataRoots::standard(root.path()).document("ui/help.json").find("help")->string() ==
        "Vibration einstellen.");
  root.write("mods/data/ui/help.json", R"({"help": "{xbe:0x19b900}"})");
  CHECK_THROWS(DataRoots::standard(root.path()).document("ui/help.json"));
}

TEST_CASE("game text references list their addresses") {
  CHECK((phantomcrash::game_text_references("{xbe:0x19b848} and {xbe:0x0001a2}{xbe:0x}") ==
         std::vector<std::uint32_t>{0x19b848, 0x1a2}));
  CHECK(phantomcrash::game_text_reference(0x1a2) == "{xbe:0x0001a2}");
}

TEST_CASE("assets resolve through mods first, ignoring case") {
  TempFolder root;
  root.write("artifacts/europe/files/Media/sound/AWAKETO1.WAV", "media");
  root.write("artifacts/europe/assets/textures/scroll/title_tex/0.png", "converted");
  const auto roots = DataRoots::standard(root.path());
  CHECK(roots.find_asset("sound/awaketo1.wav").has_value());
  CHECK(roots.find_asset("Textures/Scroll/TITLE_TEX/0.png").has_value());
  CHECK(!roots.find_asset("sound/missing.wav").has_value());
  CHECK_THROWS(roots.asset("sound/missing.wav"));
  CHECK_THROWS(roots.find_asset("../escape.wav"));

  root.write("mods/sound/AWAKETO1.WAV", "mod");
  const auto replaced = roots.asset("sound/AWAKETO1.WAV");
  CHECK(replaced.parent_path().parent_path() == root.path() / "mods");
}

TEST_CASE("shipped roots leave out the mod folder") {
  TempFolder root;
  root.write("data/ui/title.json", R"({"idle": {"seconds": 30.0}})");
  root.write("mods/data/ui/title.json", R"({"idle": {"seconds": 5.0}})");
  root.write("artifacts/europe/files/Media/sound/AWAKETO1.WAV", "media");
  root.write("mods/sound/AWAKETO1.WAV", "mod");
  const auto roots = DataRoots::shipped(root.path());
  CHECK(roots.document("ui/title.json").find("idle")->number_or("seconds", 0) == 30.0);
  CHECK(roots.asset("sound/AWAKETO1.WAV").parent_path().parent_path() ==
        root.path() / "artifacts" / "europe" / "files" / "Media");
}

TEST_CASE("the data root is found from a folder inside it") {
  TempFolder root;
  root.write("data/boot/intro.json", "{}");
  fs::create_directories(root.path() / "build" / "bin");
  const auto found = DataRoots::find_root({root.path() / "build" / "bin"});
  CHECK(found.has_value());
  CHECK(fs::equivalent(*found, root.path()));
}

TEST_CASE("the shipped data documents load and name known buttons") {
  const auto roots = DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT);
  for (const char *name : {"boot/intro.json", "ui/title.json", "ui/main_menu.json",
                           "audio/music.json", "audio/sfx.json"}) {
    CHECK(roots.document(name).find("notes") != nullptr);
  }
  for (const auto &step : roots.document("boot/intro.json").find("steps")->array()) {
    if (const auto *skip = step.find("skip")) {
      static_cast<void>(phantomcrash::buttons_from_json(*skip->find("buttons")));
    }
  }
  const auto menu = roots.document("ui/main_menu.json");
  static_cast<void>(phantomcrash::buttons_from_json(*menu.find("cancel")->find("buttons")));
  const auto title = roots.document("ui/title.json");
  static_cast<void>(phantomcrash::buttons_from_json(*title.find("start")->find("buttons")));
}

TEST_CASE("SLAI content is opt-in and legacy overlays cannot bypass its toggle") {
  TempFolder root;
  root.write("data/test.json", R"({"value":1})");
  root.write("mods/data/test.json", R"({"value":2})");
  root.write("artifacts/europe/assets/model.txt", "base");
  CHECK(DataRoots::configured(root.path(), false).document("test.json").number_or("value", 0) == 2);
  root.write("mods/slai/native_parts.json", "{}");
  CHECK(DataRoots::configured(root.path(), false).document("test.json").number_or("value", 0) == 1);
  CHECK(!phantomcrash::slai_installed(root.path()));
  root.write("artifacts/slai/assets/installed.json", R"({"version":1,"disc":"SLES_529.40"})");
  for (auto file : {"tables/parts.json", "tables/garage.json", "robots/catalogue.json",
                    "slai/native_parts.json", "slai/assets/manifest.json"}) {
    root.write(fs::path("artifacts/slai/assets") / file, "{}");
  }
  root.write("artifacts/slai/assets/data/test.json", R"({"value":3})");
  root.write("artifacts/slai/assets/model.txt", "imported");
  CHECK(phantomcrash::slai_installed(root.path()));
  CHECK(DataRoots::configured(root.path(), true).document("test.json").number_or("value", 0) == 3);
  CHECK(DataRoots::configured(root.path(), true).asset("model.txt") ==
        root.path() / "artifacts/slai/assets/model.txt");
  CHECK(DataRoots::configured(root.path(), false).asset("model.txt") ==
        root.path() / "artifacts/europe/assets/model.txt");
}

TEST_CASE("content modes retain original saves and use independent campaign files") {
  TempFolder root;
  const auto base = root.path() / "settings.json";
  phantomcrash::Settings original;
  original.quest.player_name = "Original";
  original.online_profile = original.quest;
  CHECK(phantomcrash::save_settings(base, original));
  CHECK(!phantomcrash::load_settings(base).slai_enabled);
  const auto modded = phantomcrash::content_profile_path(base, true, false);
  const auto clean = phantomcrash::content_profile_path(base, false, true);
  CHECK(modded != base && clean != base && modded != clean);
  auto fresh = phantomcrash::load_content_settings(base, modded, true);
  CHECK(fresh.quest.player_name.empty() && !fresh.online_profile);
  fresh.quest.player_name = "SLAI";
  CHECK(phantomcrash::save_settings(modded, fresh));
  CHECK(phantomcrash::load_content_settings(base, modded, true).quest.player_name == "SLAI");
  CHECK(phantomcrash::load_settings(base).quest.player_name == "Original");
  CHECK(phantomcrash::content_profile_path(base, false, false) == base);
  CHECK(phantomcrash::load_content_settings(base, clean, false).quest.player_name.empty());
}
