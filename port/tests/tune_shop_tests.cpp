// PlusTech's CUSTOMIZE: the icons, the player's parts listed, the level
// meter, the price of a tune-up and a body's modules taken off.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/tune_shop.hpp"

#include <string>
#include <vector>

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

TuneShopScript shipped_script() {
  return parse_tune_shop(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/customize.json"));
}

PartStatsTable shipped_table() {
  return parse_part_stats(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/part_stats.json"));
}

// Prices 100 NY a level from 1000 either way; 4 socket holes, one fewer a
// level lighter.
PartTables tables() {
  PartTables parts;
  parts.bodies.assign(1, ArmourRecord{});
  parts.bodies[0].name = "BODY";
  parts.bodies[0].price = {1000.0F, 0.0F, 9900.0F, 0, 1};
  parts.bodies[0].sockets = {4.0F, 0.0F, 99.0F, 0, -1};
  parts.arm_weapons.assign(2, WeaponRecord{});
  parts.arm_weapons[0].name = "GUN-B";
  parts.arm_weapons[1].name = "GUN-A";
  for (WeaponRecord &weapon : parts.arm_weapons) {
    weapon.price = {1000.0F, 0.0F, 9900.0F, 0, 1};
  }
  return parts;
}

GarageTables garage_tables() {
  GarageTables garage;
  garage.modules.assign(1, ModuleRecord{});
  garage.modules[0].name = "O.C";
  garage.modules[0].sockets = 3;
  return garage;
}

struct Shop {
  PartTables parts = tables();
  GarageTables garage = garage_tables();
  PartStatsTable table = shipped_table();
};

void settle(TuneShop &logic, const double seconds = 2.0) {
  for (double time = 0.0; time < seconds; time += tick) {
    logic.update(tick, InputFrame{});
  }
}

void tap(TuneShop &logic, const Buttons buttons, const int times = 1) {
  for (int count = 0; count < times; ++count) {
    logic.update(tick, InputFrame{buttons, buttons, 1});
    settle(logic, 0.6);
  }
}

// A body fitted in hangar 0 with a module in it, and two arm weapons.
QuestSave owned(const std::int64_t money) {
  QuestSave quest;
  quest.money = money;
  Garage &garage = quest.garage;
  garage.hangars[0].owned = true;
  const int body = garage.add_stock({PartKind::body, 0, 0, "", 0});
  const int module = garage.add_stock({PartKind::module, 0, 0, "", 0});
  garage.hangars[0].parts[static_cast<std::size_t>(PartSlot::body)] = body;
  garage.hangars[0].modules = {module};
  const int right = garage.add_stock({PartKind::arm, 0, 5, "", 0});
  garage.hangars[0].parts[static_cast<std::size_t>(PartSlot::left_arm)] = right;
  garage.add_stock({PartKind::arm, 1, 0, "", -1});
  return quest;
}

TuneShop opened(const Shop &shop, const std::int64_t money) {
  TuneShop logic(shipped_script(), owned(money), shop.parts, shop.garage, shop.table);
  settle(logic);
  return logic;
}

} // namespace

TEST_CASE("customize.json reads the icons, EXIT last, the meter and the messages") {
  const TuneShopScript script = shipped_script();
  REQUIRE(script.items.size() == 5);
  CHECK(script.items[0].kind == GoodsKind::body);
  CHECK(script.items[1].kind == GoodsKind::arm);
  CHECK(script.items[3].kind == GoodsKind::legs);
  CHECK(!script.items.back().kind.has_value());
  CHECK(script.level_min == -99);
  CHECK(script.level_max == 99);
  CHECK(script.list.side_marks.size() == 2);
  CHECK(script.messages.count("dismantled") == 1);
  CHECK(script.exit_to == "plustech");
  CHECK(!script.meter.help.empty());
}

TEST_CASE("a tune-up costs the difference, through level 0 when it crosses it") {
  const Shop shop;
  const Goods five{GoodsKind::arm, 0, 5};
  CHECK(tune_price(shop.parts, shop.garage, five, 8) == 300);
  CHECK(tune_price(shop.parts, shop.garage, five, 2) == 300);
  CHECK(tune_price(shop.parts, shop.garage, five, -3) == 800);
}

TEST_CASE("the icons do not wrap; cancel jumps to EXIT and EXIT leaves") {
  const Shop shop;
  TuneShop logic = opened(shop, 0);
  const TuneShopScript &script = logic.script();
  tap(logic, script.left);
  CHECK(logic.item() == 0);
  tap(logic, script.cancel_buttons);
  CHECK(logic.item() == 4);
  tap(logic, script.right);
  CHECK(logic.item() == 4);
  tap(logic, script.confirm_buttons);
  settle(logic, 3.0);
  CHECK(logic.done());
  CHECK(logic.next() == "plustech");
}

TEST_CASE("an arm weapon tuned heavier, paid for and kept at its new level") {
  const Shop shop;
  TuneShop logic = opened(shop, 5000);
  const TuneShopScript &script = logic.script();
  tap(logic, script.right);
  tap(logic, script.confirm_buttons);
  REQUIRE(logic.view() == TuneShopView::list);
  REQUIRE(logic.goods_list().goods().size() == 2);
  CHECK(logic.chosen()->name == "GUN-A"); // by name
  tap(logic, script.down);
  REQUIRE(logic.chosen()->name == "GUN-B");
  CHECK(logic.chosen()->side == 1); // the second of the pair of arm slots
  tap(logic, script.confirm_buttons);
  REQUIRE(logic.view() == TuneShopView::meter);
  CHECK(logic.help_text() == script.meter.help);
  CHECK(logic.fitted_load().has_value());
  tap(logic, script.confirm_buttons); // the same level asks nothing
  CHECK(logic.view() == TuneShopView::meter);
  tap(logic, script.right, 3);
  CHECK(logic.level() == 8);
  CHECK(logic.price() == 300);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "tune");
  CHECK(logic.choice() == 1);
  tap(logic, script.left);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "tuned");
  CHECK(logic.quest().money == 4700);
  CHECK(logic.quest().garage.stock.at(2).level == 8);
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == TuneShopView::list);
  CHECK(logic.chosen()->goods.level == 8);
}

TEST_CASE("too little money; cancel puts the level back") {
  const Shop shop;
  TuneShop logic = opened(shop, 100);
  const TuneShopScript &script = logic.script();
  tap(logic, script.confirm_buttons);
  tap(logic, script.confirm_buttons);
  tap(logic, script.right, 2);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "poor");
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == TuneShopView::meter);
  CHECK(logic.level() == 2);
  tap(logic, script.cancel_buttons);
  CHECK(logic.view() == TuneShopView::list);
  CHECK(logic.level() == 0);
  CHECK(logic.quest().money == 100);
}

TEST_CASE("a body tuned below its modules' socket holes has them taken off") {
  const Shop shop;
  TuneShop logic = opened(shop, 5000);
  const TuneShopScript &script = logic.script();
  tap(logic, script.confirm_buttons);
  REQUIRE(logic.view() == TuneShopView::list);
  tap(logic, script.confirm_buttons);
  tap(logic, script.left, 2); // 2 holes; the module takes 3
  tap(logic, script.confirm_buttons);
  tap(logic, script.left);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "dismantled");
  CHECK(logic.quest().garage.hangars[0].modules.empty());
  CHECK(logic.quest().garage.stock.at(1).hangar == -1);
  CHECK(logic.quest().money == 4800);
}

TEST_CASE("with no parts of a kind the shop says so and goes back to the icons") {
  const Shop shop;
  TuneShop logic = opened(shop, 0);
  const TuneShopScript &script = logic.script();
  tap(logic, script.right, 3); // legs
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "none");
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == TuneShopView::items);
  CHECK(!logic.list_open());
}
