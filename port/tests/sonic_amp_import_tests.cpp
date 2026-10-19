#include "test_support.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/sonic_amp.hpp"

using namespace phantomcrash;

namespace {
void settle_import(SonicAmp &shop) {
  for (int i = 0; i < 40; ++i) {
    shop.update(1.0 / 60.0, {});
  }
}
void press_import(SonicAmp &shop, const Buttons button) {
  shop.update(1.0 / 60.0, InputFrame{button, button, 1});
  settle_import(shop);
}
} // namespace

TEST_CASE("imported songs sell and preview by credit id without exposing special music") {
  auto script =
      parse_sonic_amp(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/sonic_amp.json"));
  script.grid.category_pages = {2, 2, 10};
  script.grid.catalogue_ids.clear();
  for (int id = 0; id < 78; ++id) {
    script.grid.catalogue_ids.push_back(id);
  }
  for (int id = 81; id < 171; ++id) {
    script.grid.catalogue_ids.push_back(id);
  }
  script.grid.catalogue_tracks = 168;
  std::vector<MusicCredit> credits(171);
  credits[81] = {"sound/slai/bgm_00.ogg", "Imported song", "Imported artist"};
  SonicAmp shop(script, MusicLibrary{}, 450, credits, "town_map");
  settle_import(shop);
  for (int i = 0; i < 3; ++i) {
    press_import(shop, button::right);
  }
  press_import(shop, button::a);
  CHECK(shop.pages() == 10);
  for (int i = 0; i < 2; ++i) {
    press_import(shop, button::right_trigger);
  }
  CHECK(shop.cursor_track() == 72);
  CHECK(shop.slot_track(1, 0) == 81);
  press_import(shop, button::down);
  CHECK(shop.cursor_track() == 81);
  press_import(shop, button::a);
  press_import(shop, button::a);
  CHECK(shop.view() == SonicAmpView::preview);
  CHECK(shop.music_track() == "sound/slai/bgm_00.ogg");
  press_import(shop, button::b);
  press_import(shop, button::a);
  press_import(shop, button::left);
  press_import(shop, button::a);
  CHECK(shop.music().owns(81));
  CHECK(shop.money() == 250);
  press_import(shop, button::a);
  for (int i = 0; i < 7; ++i) {
    press_import(shop, button::right_trigger);
  }
  CHECK(shop.slot_track(1, 5) == 170);
}
