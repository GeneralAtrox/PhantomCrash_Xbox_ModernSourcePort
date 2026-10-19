#include "test_support.hpp"
#include "../src/desktop_coop_storage.hpp"
#include <chrono>
#include <filesystem>

using namespace phantomcrash;

TEST_CASE("GUI network co-op saves its online profile without replacing local profiles") {
  struct Files {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("phantom-lan-save-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Files() {
      std::filesystem::create_directory(root);
    }
    ~Files() {
      std::filesystem::remove_all(root);
    }
  } files;
  const auto path = files.root / "settings.json";
  Settings original;
  QuestSave local, network;
  local.player_name = "Local";
  network.player_name = "Online";
  original.coop_profiles[0] = local;
  original.online_profile = network;
  CHECK(save_settings(path, original));
  auto state = make_coop_state(Options{}, original, path, files.root, false);
  state->online_mode = true;
  network.money = 12345;
  CHECK(state->save_profile(2, network));
  auto disk = load_settings(path);
  REQUIRE(disk.online_profile);
  CHECK(disk.online_profile->money == 12345);
  REQUIRE(disk.coop_profiles[0]);
  CHECK(disk.coop_profiles[0]->player_name == "Local");
  CHECK(!disk.coop_profiles[2]);
  state->online_mode = false;
  local.money = 678;
  CHECK(state->save_profile(1, local));
  disk = load_settings(path);
  REQUIRE(disk.coop_profiles[1]);
  CHECK(disk.coop_profiles[1]->money == 678);
  CHECK(disk.online_profile->money == 12345);
  state->online_profile = network;
  state->profiles[1] = local;
  Settings copied = original;
  copy_coop_profiles(*state, copied);
  CHECK(copied.online_profile->money == 12345);
  REQUIRE(copied.coop_profiles[1]);
  CHECK(copied.coop_profiles[1]->money == 678);
}
