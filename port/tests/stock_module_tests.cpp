// The garage's STOCK MODULE: the parts shop's grid and lists of the
// player's goods, showing them without trading.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/module_shop.hpp"

#include <string>

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

ModuleShopScript shipped_script() {
  const DataRoots roots = DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT);
  const ModuleShopScript shop =
      parse_module_shop(roots.document("ui/module_shop.json"), ModuleShopMode::sell);
  return parse_stock_module(roots.document("ui/stock_module.json"), shop);
}

PartStatsTable shipped_table() {
  return parse_part_stats(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/part_stats.json"));
}

void settle(ModuleShop &logic, const double seconds = 2.0) {
  for (double time = 0.0; time < seconds; time += tick) {
    logic.update(tick, InputFrame{});
  }
}

void tap(ModuleShop &logic, const Buttons buttons) {
  logic.update(tick, InputFrame{buttons, buttons, 1});
  settle(logic, 0.6);
}

struct Stock {
  PartTables parts;
  GarageTables garage;
  PartStatsTable table = shipped_table();

  Stock() {
    parts.bodies.assign(1, ArmourRecord{});
    parts.bodies[0].name = "BODY";
    parts.bodies[0].price.base = 1000.0F;
  }
};

// One body in the stock, fitted nowhere.
ModuleShop browsing(const Stock &stock) {
  QuestSave quest;
  quest.money = 100;
  quest.garage = new_game_garage(garage_new_game_hangars);
  static_cast<void>(quest.garage.add_stock({PartKind::body, 0, 0, "", 1}));
  ModuleShop logic(shipped_script(), quest, stock.parts, stock.garage, stock.table);
  settle(logic);
  return logic;
}

} // namespace

TEST_CASE("stock_module.json reads a grid of ten, the upper EXIT without an icon") {
  const ModuleShopScript script = shipped_script();
  CHECK(script.mode == ModuleShopMode::stock);
  CHECK(script.scene == 38);
  CHECK(script.exit_to == "garage");
  CHECK(script.columns == 5);
  REQUIRE(script.categories.size() == 10);
  CHECK(script.categories[3].kind == std::optional<GoodsKind>{GoodsKind::chip});
  CHECK(!script.categories[8].kind.has_value());
  CHECK(!script.categories[8].shown);
  CHECK(script.categories[9].shown);
  CHECK(script.side.pictures.empty());
  CHECK(script.list.rows == 6);
  // The descriptions and swatches are the parts shop's.
  CHECK(!script.goods_help[static_cast<std::size_t>(GoodsKind::module)].empty());
  CHECK(!script.paint_swatches.empty());
}

TEST_CASE("the stock's lists show the player's goods; confirm trades nothing") {
  const Stock stock;
  ModuleShop logic = browsing(stock);
  const ModuleShopScript &script = logic.script();
  CHECK(logic.category() == 0);
  tap(logic, script.confirm_buttons);
  REQUIRE(logic.view() == ModuleShopView::list);
  REQUIRE(logic.goods().size() == 1);
  CHECK(logic.goods()[0].name == "BODY");
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == ModuleShopView::list);
  CHECK(logic.quest().money == 100);
  CHECK(logic.quest().garage.stock.size() == 1);
  tap(logic, script.cancel_buttons);
  CHECK(logic.view() == ModuleShopView::categories);
  // The legs' list opens empty rather than saying so.
  tap(logic, script.down);
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == ModuleShopView::list);
  CHECK(logic.goods().empty());
  tap(logic, script.cancel_buttons);
  // Cancel jumps to the lower EXIT, which goes back to the garage.
  tap(logic, script.cancel_buttons);
  CHECK(logic.category() == 9);
  tap(logic, script.confirm_buttons);
  settle(logic);
  REQUIRE(logic.done());
  CHECK(logic.next() == "garage");
}
