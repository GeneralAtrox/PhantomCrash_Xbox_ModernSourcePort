// Wild Machines' CHIP shops: the day's CHIPs and the player's, sliding the
// cards, buying one into the stock to be named, and selling one.

#include "test_support.hpp"

#include "phantomcrash/chip_shop.hpp"
#include "phantomcrash/chip_spec_rows.hpp"
#include "phantomcrash/data_roots.hpp"

#include <string>
#include <vector>

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

ChipShopScript shipped_script(const ChipShopMode mode) {
  return parse_chip_shop(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/chip_shop.json"),
                         mode);
}

// The dialog as the original's: no browsing, opening on CANCEL.
ChipShopScript original_script(const ChipShopMode mode) {
  ChipShopScript script = shipped_script(mode);
  script.dialog.browse.reset();
  return script;
}

PartTables tables() {
  PartTables parts;
  parts.chips.assign(3, ChipRecord{});
  parts.chips[0].name = "CHIP-A";
  parts.chips[1].name = "CHIP-B";
  parts.chips[2].name = "CHIP-C";
  for (ChipRecord &chip : parts.chips) {
    chip.price.base = 1000.0F;
    chip.weight.base = 150.0F;
  }
  return parts;
}

std::vector<ChipOffer> for_sale() {
  return {{0, 1, "CHIP-A", 1000}, {1, 1, "CHIP-B", 1000}, {2, 1, "CHIP-C", 1000}};
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

ChipShop buying(const PartTables &parts, const std::int64_t money, const bool original = false) {
  QuestSave quest;
  quest.money = money;
  ChipShop logic(original ? original_script(ChipShopMode::buy) : shipped_script(ChipShopMode::buy),
                 for_sale(), quest, parts);
  settle(logic);
  return logic;
}

} // namespace

TEST_CASE("chip_shop.json reads both modes, with their options and notices") {
  const ChipShopScript buy = shipped_script(ChipShopMode::buy);
  CHECK(buy.dialog.options.size() == 2);
  CHECK(buy.messages.count("name_chip") == 1);
  CHECK(buy.cards.side == 3);
  CHECK(buy.exit_to == "wild_machines");
  const ChipShopScript sell = shipped_script(ChipShopMode::sell);
  CHECK(sell.messages.count("sold") == 1);
  CHECK(sell.sell_rate == 0.5);
  CHECK(sell.labels.size() == buy.labels.size());
  CHECK(buy.dialog.browse);
  CHECK(sell.dialog.browse);
}

TEST_CASE("the day's lineup and the player's CHIPs become offers") {
  const PartTables parts = tables();
  GarageTables garage;
  garage.chip_lineups = {{{0, 2}, {7, 1}, {2, 5}}};
  const auto offers = chips_for_sale(parts, garage, 0);
  CHECK(offers.size() == 2); // record 7 does not exist
  CHECK(offers[1].chip == 2);
  CHECK(offers[1].level == 5);
  CHECK(chips_for_sale(parts, garage, 3).empty());
  Garage owned;
  owned.add_stock({PartKind::body, 0, 1, "BODY", -1});
  owned.add_stock({PartKind::chip, 1, 3, "MINE", 0});
  const auto mine = chips_in_stock(parts, garage, owned);
  CHECK(mine.size() == 1);
  CHECK(mine[0].name == "MINE");
  CHECK(mine[0].stock == 1);
  CHECK(mine[0].equipped);
}

TEST_CASE("the cards slide and wrap, and up moves to EXIT") {
  const PartTables parts = tables();
  ChipShop logic = buying(parts, 0);
  CHECK(logic.settled());
  CHECK(logic.window_open() == 1.0F);
  const ChipShopScript &script = logic.script();
  tap(logic, script.left);
  CHECK(logic.cursor() == 2);
  CHECK(logic.previous() == 0);
  tap(logic, script.right);
  CHECK(logic.cursor() == 0);
  tap(logic, script.up);
  CHECK(logic.on_exit());
  tap(logic, script.confirm_buttons);
  settle(logic, 3.0);
  CHECK(logic.done());
  CHECK(logic.next() == "wild_machines");
}

TEST_CASE("buying a CHIP puts it in the stock and names it") {
  const PartTables parts = tables();
  ChipShop logic = buying(parts, 5000);
  const ChipShopScript &script = logic.script();
  tap(logic, script.right);
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == ChipShopView::dialog);
  CHECK(logic.on_card());
  tap(logic, script.confirm_buttons);
  CHECK(!logic.on_card());
  CHECK(logic.choice() == 0);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "thanks");
  CHECK(logic.quest().money == 4000);
  const StockItem &item = logic.quest().garage.stock.at(logic.naming_stock());
  CHECK(item.kind == PartKind::chip);
  CHECK(item.id == 1);
  CHECK(item.name == "CHIP-B");
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "name_chip");
  tap(logic, script.confirm_buttons);
  settle(logic, 3.0);
  CHECK(logic.done());
  CHECK(logic.next() == "name_entry");
}

TEST_CASE("without the money the CHIP stays, and CANCEL goes back to the cards") {
  const PartTables parts = tables();
  ChipShop logic = buying(parts, 10, true);
  const ChipShopScript &script = logic.script();
  tap(logic, script.confirm_buttons);
  CHECK(logic.choice() == 1);
  tap(logic, script.cancel_buttons); // already on CANCEL: stays
  CHECK(logic.view() == ChipShopView::dialog);
  tap(logic, script.left);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "poor");
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == ChipShopView::browse);
  CHECK(logic.highlight_pulses());
  CHECK(logic.quest().garage.stock.empty());
  tap(logic, script.confirm_buttons);
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == ChipShopView::browse);
}

TEST_CASE("selling a loose CHIP pays half its price; a fitted one cannot be sold") {
  const PartTables parts = tables();
  QuestSave quest;
  quest.money = 0;
  quest.garage.add_stock({PartKind::chip, 0, 1, "FITTED", 0});
  quest.garage.add_stock({PartKind::chip, 2, 1, "LOOSE", -1});
  std::vector<ChipOffer> offers = {{0, 1, "FITTED", 1000, 0, true},
                                   {2, 1, "LOOSE", 1000, 1, false}};
  ChipShop logic(original_script(ChipShopMode::sell), offers, quest, parts);
  settle(logic);
  const ChipShopScript &script = logic.script();
  tap(logic, script.confirm_buttons);
  tap(logic, script.left);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "equipped");
  tap(logic, script.confirm_buttons);
  tap(logic, script.right);
  CHECK(logic.cursor() == 1);
  tap(logic, script.confirm_buttons);
  tap(logic, script.left);
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == ChipShopView::question);
  CHECK(logic.choice() == 1);
  tap(logic, script.left);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "sold");
  tap(logic, script.confirm_buttons);
  CHECK(logic.quest().money == 500);
  CHECK(logic.quest().garage.stock.size() == 1);
  CHECK(logic.offers().size() == 1);
  CHECK(logic.cursor() == 0);
}

TEST_CASE("the dialog opens on the card, whose arrows step through the CHIPs") {
  const PartTables parts = tables();
  ChipShop logic = buying(parts, 5000);
  const ChipShopScript &script = logic.script();
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == ChipShopView::dialog);
  CHECK(logic.on_card());
  tap(logic, script.right);
  CHECK(logic.cursor() == 1);
  CHECK(logic.dialog_open());
  tap(logic, script.left);
  tap(logic, script.left);
  CHECK(logic.cursor() == 2);
  CHECK(logic.card_slide() == 1.0F);
  tap(logic, script.down);
  CHECK(!logic.on_card());
  CHECK(logic.choice() == 0);
  tap(logic, script.right);
  CHECK(logic.choice() == 1);
  CHECK(logic.cursor() == 2); // left/right on the options leave the CHIP
  tap(logic, script.up);
  CHECK(logic.on_card());
  tap(logic, script.cancel_buttons);
  CHECK(logic.view() == ChipShopView::browse);
  CHECK(logic.cursor() == 2);
  CHECK(logic.previous() == 2); // the cards do not slide on leaving
  tap(logic, script.confirm_buttons);
  tap(logic, script.right);
  tap(logic, script.down);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "thanks");
  CHECK(logic.quest().garage.stock.at(logic.naming_stock()).id == 0);
}

TEST_CASE("the dialog compares with the CHIP fitted to the robot ridden") {
  const PartTables parts = tables();
  QuestSave quest;
  quest.garage.riding = 0;
  quest.garage.add_stock({PartKind::chip, 2, 4, "", 0});
  quest.garage.add_stock({PartKind::chip, 1, 1, "SPARE", -1});
  const ChipShop logic(shipped_script(ChipShopMode::buy), for_sale(), quest, parts);
  const auto fitted = logic.fitted();
  REQUIRE(fitted);
  CHECK(fitted->chip == 2);
  CHECK(fitted->level == 4);
  CHECK(fitted->name == "CHIP-C"); // unnamed: its record's name
  CHECK(fitted->stock == 0);
  REQUIRE(logic.script().dialog.rows);
  CHECK(logic.script().dialog.rows->compare);
  quest.garage.riding = 1; // a hangar without a CHIP
  CHECK(!ChipShop(shipped_script(ChipShopMode::buy), for_sale(), quest, parts).fitted());
  CHECK(chip_change(20, 37, -1) == ChipChange::better); // a quicker lock
  CHECK(chip_change(30, 35, 1) == ChipChange::worse);   // less knockdown resistance
  CHECK(chip_change(2, 2, 1) == ChipChange::same);
  CHECK(chip_change(3, 1, 0) == ChipChange::differs);
}

TEST_CASE("with nothing to sell SELL CHIP says so and goes back") {
  const PartTables parts = tables();
  ChipShop logic(shipped_script(ChipShopMode::sell), {}, QuestSave{}, parts);
  settle(logic);
  CHECK(logic.message() == "none");
  tap(logic, logic.script().confirm_buttons);
  settle(logic, 3.0);
  CHECK(logic.done());
  CHECK(logic.next() == "wild_machines");
}

TEST_CASE("the dialog shows twice the reticles' half sizes a battle uses") {
  ChipSpecPanel panel;
  panel.inner_sizes[1] = {60.0F, 180.0F, 20.0F, 60.0F};
  panel.outer_sizes[0] = {70.0F, 160.0F, 70.0F, 160.0F};
  ChipRecord chip;
  chip.inner_shape = 1;
  chip.inner_value.base = 999.0F;
  chip.outer_value.base = 0.0F;
  chip.critical_percent.base = 12.0F;
  chip.weight.base = 150.0F;
  const ChipSpec spec = chip_spec(panel, chip, 1);
  CHECK(spec.inner_shape == 1);
  CHECK(spec.inner_half[0] == 180.0F);
  CHECK(spec.inner_half[1] == 60.0F);
  CHECK(spec.outer_half[0] == 70.0F);
  CHECK(spec.mortal_shot == 12);
  CHECK(spec.weight == 150);
}
