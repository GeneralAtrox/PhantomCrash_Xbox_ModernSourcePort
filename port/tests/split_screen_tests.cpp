// Split screen: the screens' rectangles and canvases, and the split
// panels' layouts.

#include "test_support.hpp"

#include "phantomcrash/battle_panels_style.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/split_screen.hpp"

using namespace phantomcrash;

namespace {

json::JsonValue shipped(const char *relative) {
  return DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document(relative);
}

} // namespace

TEST_CASE("one screen fills the window with the whole canvas") {
  const std::vector<SplitScreen> screens = split_screens(1, 1920, 1080);
  CHECK(screens.size() == 1);
  if (screens.size() != 1) {
    return;
  }
  CHECK(screens[0].rect.width == 1920.0F);
  CHECK(screens[0].canvas[1] == 480.0F);
}

TEST_CASE("two screens are the halves and three or four the quarters") {
  const std::vector<SplitScreen> two = split_screens(2, 1920, 1081);
  CHECK(two.size() == 2);
  if (two.size() != 2) {
    return;
  }
  CHECK(two[1].rect.y == 540.0F);
  CHECK(two[0].rect.height == two[1].rect.height);
  CHECK(two[0].canvas[0] == 640.0F);
  CHECK(two[0].canvas[1] == 240.0F);
  const std::vector<SplitScreen> four = split_screens(4, 1920, 1080);
  CHECK(four.size() == 4);
  if (four.size() != 4) {
    return;
  }
  CHECK(four[1].rect.x == 960.0F);
  CHECK(four[3].rect.y == 540.0F);
  CHECK(four[2].canvas[0] == 320.0F);
  CHECK(split_screens(3, 1920, 1080).size() == 3);
  CHECK(split_screens(9, 1920, 1080).size() == 4);
}

TEST_CASE("a screen's canvas keeps the single screen's scale") {
  const std::vector<SplitScreen> two = split_screens(2, 1920, 1080);
  const Viewport lower = fit_screen(two[1].rect, two[1].canvas[0], two[1].canvas[1]);
  CHECK(lower.scale == fit_canvas(1920, 1080).scale);
  CHECK(lower.to_pixel_y(0.0F) == 540.0F);
  CHECK(lower.visible_middle() == 120.0F);
  CHECK(lower.visible_left() < 0.0F);
}

TEST_CASE("the split panels leave out the labels, messages and bars") {
  const BattlePanelsStyle two = parse_battle_panels_style(shipped("ui/battle_panels_split2.json"));
  CHECK(two.canvas[1] == 240.0F);
  CHECK(!two.labels);
  CHECK(!two.messages_shown);
  CHECK(!two.weapons.left.bar.has_value());
  CHECK(two.weapons.right.arm[0] == 512.0F);
  CHECK(two.durability.width == 102.0F);
  const BattlePanelsStyle four = parse_battle_panels_style(shipped("ui/battle_panels_split4.json"));
  CHECK(four.canvas[0] == 320.0F);
  CHECK(four.optic.fill.position[0] == 191.0F);
  CHECK(four.weapons.right.arm[0] == 242.0F);
  CHECK(anchor_x(HudAnchor::right, 242.0F, -50.0F, 370.0F, four.canvas[0]) == 292.0F);
  const BattlePanelsStyle one = parse_battle_panels_style(shipped("ui/battle_panels.json"));
  CHECK(one.labels);
  CHECK(one.weapons.left.bar.has_value());
}
