// PlusTech's UPLIFT: the player's CHIPs on the cards, a level chosen in the
// level window and paid for by the difference in price.

#include "test_support.hpp"

#include "phantomcrash/chip_shop.hpp"
#include "phantomcrash/data_roots.hpp"

#include <string>
#include <vector>

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

ChipShopScript shipped_script() {
  return parse_chip_shop(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/uplift.json"),
                         ChipShopMode::uplift);
}

PartTables tables() {
  PartTables parts;
  parts.chips.assign(2, ChipRecord{});
  parts.chips[0].name = "CHIP-A";
  parts.chips[1].name = "CHIP-B";
  for (ChipRecord &chip : parts.chips) {
    chip.price.base = 1000.0F; // 100 more a level
    chip.price.top = 9900.0F;
  }
  return parts;
}

void settle(ChipShop &logic, const double seconds = 2.0) {
  for (double time = 0.0; time < seconds; time += tick) {
    logic.update(tick, InputFrame{});
  }
}

void tap(ChipShop &logic, const Buttons buttons) {
  logic.update(tick, InputFrame{buttons, buttons, 1});
  settle(logic, 0.6);
}

ChipShop uplifting(const PartTables &parts, const int level, const std::int64_t money) {
  QuestSave quest;
  quest.money = money;
  quest.garage.add_stock({PartKind::chip, 1, level, "MINE", -1});
  GarageTables garage;
  ChipShop logic(shipped_script(), chips_in_stock(parts, garage, quest.garage), quest, parts);
  settle(logic);
  return logic;
}

} // namespace

TEST_CASE("uplift.json reads the level window in place of the options") {
  const ChipShopScript script = shipped_script();
  CHECK(script.mode == ChipShopMode::uplift);
  CHECK(script.dialog.options.empty());
  CHECK(script.dialog.level.has_value());
  CHECK(script.level_max == 99);
  CHECK(script.messages.count("maxed") == 1);
  CHECK(script.exit_to == "plustech");
  CHECK(!script.dialog_help.empty());
}

TEST_CASE("uplifting a CHIP pays the difference and raises it in the stock") {
  const PartTables parts = tables();
  ChipShop logic = uplifting(parts, 5, 5000);
  const ChipShopScript &script = logic.script();
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == ChipShopView::dialog);
  CHECK(logic.level() == 5);
  CHECK(logic.help_text() == script.dialog_help);
  tap(logic, script.left); // not below its own level
  CHECK(logic.level() == 5);
  tap(logic, script.confirm_buttons); // the same level asks nothing
  CHECK(logic.view() == ChipShopView::dialog);
  for (int step = 0; step < 3; ++step) {
    tap(logic, script.right);
  }
  CHECK(logic.level() == 8);
  CHECK(logic.uplift_price() == 300);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "uplift");
  CHECK(logic.choice() == 1);
  tap(logic, script.left);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "thanks");
  CHECK(logic.quest().money == 4700);
  CHECK(logic.quest().garage.stock.begin()->second.level == 8);
  CHECK(logic.offers()[0].level == 8);
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == ChipShopView::browse);
}

TEST_CASE("too little money, a CHIP at the top and none at all") {
  const PartTables parts = tables();
  {
    ChipShop logic = uplifting(parts, 5, 100);
    tap(logic, logic.script().confirm_buttons);
    for (int step = 0; step < 2; ++step) {
      tap(logic, logic.script().right);
    }
    tap(logic, logic.script().confirm_buttons);
    CHECK(logic.message() == "poor");
    tap(logic, logic.script().confirm_buttons);
    CHECK(logic.view() == ChipShopView::browse);
  }
  {
    ChipShop logic = uplifting(parts, 99, 5000);
    tap(logic, logic.script().confirm_buttons);
    CHECK(logic.message() == "maxed");
    CHECK(logic.choice() == 0);
    tap(logic, logic.script().confirm_buttons);
    CHECK(logic.view() == ChipShopView::dialog);
    tap(logic, logic.script().right);
    CHECK(logic.level() == 99);
    tap(logic, logic.script().cancel_buttons);
    CHECK(logic.view() == ChipShopView::browse);
  }
  QuestSave quest;
  ChipShop logic(shipped_script(), {}, quest, parts);
  settle(logic);
  CHECK(logic.message() == "none");
  tap(logic, logic.script().confirm_buttons);
  settle(logic, 3.0);
  CHECK(logic.done());
  CHECK(logic.next() == "plustech");
}
