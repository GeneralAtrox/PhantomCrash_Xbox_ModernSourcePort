// Wild Machines' BUY MODULE and SELL MODULE: the category grid, the day's
// goods and the player's, their stats, and buying and selling them.

#include "test_support.hpp"

#include "phantomcrash/battle_weapons.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/module_shop.hpp"

#include <string>
#include <vector>

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

ModuleShopScript shipped_script(const ModuleShopMode mode) {
  return parse_module_shop(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/module_shop.json"), mode);
}

PartStatsTable shipped_table() {
  return parse_part_stats(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/part_stats.json"));
}

PartTables tables() {
  PartTables parts;
  parts.bodies.assign(2, ArmourRecord{});
  parts.bodies[0].name = "BODY-B";
  parts.bodies[1].name = "BODY-A";
  parts.bodies[1].maker = 2;
  for (ArmourRecord &body : parts.bodies) {
    body.price.base = 1000.0F;
    body.weight.base = 1234.0F;
    body.armour.base = 50.0F;
    body.player_floor = 80.0F;
    body.sockets.base = 4.0F;
  }
  parts.bodies[1].price.base = 3000.0F;
  return parts;
}

GarageTables garage_tables() {
  GarageTables garage;
  garage.modules.assign(46, ModuleRecord{});
  garage.modules[3].name = "O.C";
  garage.modules[3].price = 800;
  garage.modules[45].name = "Gold Bar";
  garage.modules[45].price = 5000;
  // A lineup for the bodies: record 9 does not exist.
  garage.module_lineups = {{{0, 1}, {9, 0}, {1, -1}}};
  return garage;
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

struct Shop {
  PartTables parts = tables();
  GarageTables garage = garage_tables();
  PartStatsTable table = shipped_table();
};

ModuleShop buying(const Shop &shop, const std::int64_t money) {
  QuestSave quest;
  quest.money = money;
  // Only the bodies' cell (the first) has a lineup.
  ModuleShop logic(shipped_script(ModuleShopMode::buy), quest, shop.parts, shop.garage, shop.table,
                   {0});
  settle(logic);
  return logic;
}

ModuleShop selling(const Shop &shop, const Garage &owned) {
  QuestSave quest;
  quest.money = 100;
  quest.garage = owned;
  ModuleShop logic(shipped_script(ModuleShopMode::sell), quest, shop.parts, shop.garage,
                   shop.table);
  settle(logic);
  return logic;
}

} // namespace

TEST_CASE("module_shop.json reads both modes, a grid of eight with EXIT last") {
  const ModuleShopScript buy = shipped_script(ModuleShopMode::buy);
  CHECK(buy.categories.size() == 8);
  CHECK(buy.categories[0].kind == GoodsKind::body);
  CHECK(!buy.categories.back().kind.has_value());
  CHECK(buy.category_help.size() == 8);
  CHECK(buy.messages.count("maker_0") == 1);
  CHECK(buy.messages.count("thanks") == 1);
  CHECK(buy.exit_to == "wild_machines");
  CHECK(buy.list.hangar_badges.empty());
  const ModuleShopScript sell = shipped_script(ModuleShopMode::sell);
  CHECK(sell.messages.count("sold") == 1);
  CHECK(sell.sell_rate == 0.5);
  CHECK(sell.list.hangar_badges.size() == garage_hangar_count);
}

TEST_CASE("details windows grow below their title and divider") {
  const ModuleShopScript buy = shipped_script(ModuleShopMode::buy);
  const ModuleShopScript stock = parse_stock_module(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/stock_module.json"), buy);
  for (const ModuleShopScript *script : {&buy, &stock}) {
    for (const PopWindow &window : script->list.details) {
      REQUIRE(window.grow_caps.has_value());
      // A part's window keeps its picture frame, divider and title whole.
      CHECK((*window.grow_caps)[0] >= 148.0F);
    }
    for (const PopWindow &window : script->list.module_details) {
      REQUIRE(window.grow_caps.has_value());
      CHECK((*window.grow_caps)[0] >= 43.0F);
    }
  }
}

TEST_CASE("a leg's extra line fits above the CURRENT MUSIC plate") {
  const ModuleShopScript buy = shipped_script(ModuleShopMode::buy);
  REQUIRE(buy.extra_stats.has_value());
  // Seven lines at the closer pitch take at most the room the window has
  // below it and above it (5 pixels each).
  const float step = buy.extra_stats->part_step;
  const float raise = buy.extra_stats->part_raise;
  CHECK(step > 0.0F);
  CHECK(step < buy.list.stats.step);
  CHECK(raise <= 5.0F);
  CHECK(7.0F * step - 6.0F * buy.list.stats.step <= raise + 5.0F);
}

TEST_CASE("part_stats.json lists each kind's lines") {
  const PartStatsTable table = shipped_table();
  CHECK(table.rows[static_cast<std::size_t>(GoodsKind::legs)].size() == 6);
  CHECK(table.decimals[static_cast<std::size_t>(StatRow::weight)] == 3);
  CHECK(table.full_price == std::vector<int>{45});
}

TEST_CASE("a body's stats: weight in tonnes, armour never below the floor") {
  const Shop shop;
  const auto lines = part_stats(shop.table, shop.parts, shop.garage, {GoodsKind::body, 0, 0});
  REQUIRE(lines.size() == 4);
  CHECK(lines[1].row == StatRow::weight);
  CHECK(stat_text(lines[1]) == "12.340");
  CHECK(lines[2].row == StatRow::armour);
  CHECK(stat_text(lines[2]) == "80");
  CHECK(stat_text(lines[3]) == "4");
}

TEST_CASE("a module's extra stats show only what it changes") {
  Shop shop;
  shop.garage.modules[3].critical_percent = 10.0F;
  const auto lines = extra_stats(shop.table, shop.parts, shop.garage, {GoodsKind::module, 3, 0});
  // Its knockdown resistance is 0, so only the critical bonus shows.
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].label == "MORTAL SHOT");
  CHECK(lines[0].unit == "%");
  CHECK(lines[0].value == "10");
  CHECK(extra_stats(shop.table, shop.parts, shop.garage, {GoodsKind::module, 45, 0}).empty());
  CHECK(extra_stats(shop.table, shop.parts, shop.garage, {GoodsKind::body, 0, 0}).empty());
}

TEST_CASE("the day's lineup and the player's goods") {
  const Shop shop;
  const auto offers = goods_for_sale(shop.parts, shop.garage, GoodsKind::body, 0);
  REQUIRE(offers.size() == 2);
  CHECK(offers[1].name == "BODY-A");
  CHECK(offers[1].goods.level == -1);
  CHECK(offers[1].maker == 2);
  CHECK(goods_for_sale(shop.parts, shop.garage, GoodsKind::body, 5).empty());

  Garage owned;
  owned.add_stock({PartKind::body, 0, 0, "BODY-B", -1});
  owned.add_stock({PartKind::body, 1, 0, "BODY-A", 2});
  owned.paints = {0, 4};
  const auto bodies = goods_owned(shop.table, shop.parts, shop.garage, owned, GoodsKind::body, 0.5);
  REQUIRE(bodies.size() == 2);
  CHECK(bodies[0].name == "BODY-A"); // by name
  CHECK(bodies[0].hangar == 2);
  CHECK(bodies[0].price == 1500);
  CHECK(bodies[1].stock >= 0);
  CHECK(goods_owned(shop.table, shop.parts, shop.garage, owned, GoodsKind::paint, 0.5).size() ==
        0); // paint 4 has no entry; the ground colour is never sold
}

TEST_CASE("the grid wraps, cancel jumps to EXIT and EXIT leaves") {
  const Shop shop;
  ModuleShop logic = buying(shop, 0);
  const ModuleShopScript &script = logic.script();
  tap(logic, script.up);
  CHECK(logic.row() == 1);
  tap(logic, script.left);
  CHECK(logic.column() == 3);
  tap(logic, script.right);
  CHECK(logic.column() == 0);
  tap(logic, script.cancel_buttons);
  CHECK(logic.category() == 7);
  tap(logic, script.confirm_buttons);
  CHECK(logic.done());
  CHECK(logic.next() == "wild_machines");
}

TEST_CASE("buying: too poor, then the maker's question, paid into the stock") {
  const Shop shop;
  {
    ModuleShop logic = buying(shop, 10);
    tap(logic, logic.script().confirm_buttons);
    REQUIRE(logic.view() == ModuleShopView::list);
    REQUIRE(logic.goods().size() == 2);
    tap(logic, logic.script().confirm_buttons);
    CHECK(logic.message() == "poor");
    tap(logic, logic.script().confirm_buttons);
    CHECK(logic.view() == ModuleShopView::list);
  }
  ModuleShop logic = buying(shop, 10000);
  const ModuleShopScript &script = logic.script();
  tap(logic, script.confirm_buttons);
  tap(logic, script.down);
  CHECK(logic.list_row() == 1);
  tap(logic, script.down); // the list does not wrap
  CHECK(logic.list_row() == 1);
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == ModuleShopView::question);
  CHECK(logic.message() == "maker_2");
  CHECK(logic.choice() == 1);
  tap(logic, script.left);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "thanks");
  CHECK(logic.quest().money == 7000);
  REQUIRE(logic.quest().garage.stock.size() == 1);
  CHECK(logic.quest().garage.stock.begin()->second.name == "BODY-A");
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == ModuleShopView::list);
  tap(logic, script.cancel_buttons);
  CHECK(logic.view() == ModuleShopView::categories);
}

TEST_CASE("sorting by price turns the order round") {
  const Shop shop;
  ModuleShop logic = buying(shop, 0);
  tap(logic, logic.script().confirm_buttons);
  tap(logic, logic.script().sort_price);
  REQUIRE(logic.goods().size() == 2);
  CHECK(logic.goods()[0].price == 3000);
  tap(logic, logic.script().sort_price);
  CHECK(logic.goods()[0].price == 1000);
  tap(logic, logic.script().sort_name);
  CHECK(logic.goods()[0].name == "BODY-A");
}

TEST_CASE("selling: a fitted part is refused, the rest sold; the Gold Bar at its price") {
  const Shop shop;
  Garage owned;
  owned.add_stock({PartKind::body, 0, 0, "BODY-B", 1});
  owned.add_stock({PartKind::module, 45, 0, "Gold Bar", -1});
  {
    ModuleShop logic = selling(shop, owned);
    tap(logic, logic.script().confirm_buttons);
    REQUIRE(logic.view() == ModuleShopView::list);
    tap(logic, logic.script().confirm_buttons);
    CHECK(logic.message() == "equipped");
  }
  ModuleShop logic = selling(shop, owned);
  const ModuleShopScript &script = logic.script();
  // The options cell: column 3, top row.
  tap(logic, script.left);
  REQUIRE(script.categories[static_cast<std::size_t>(logic.category())].kind == GoodsKind::module);
  tap(logic, script.confirm_buttons);
  REQUIRE(logic.goods().size() == 1);
  CHECK(logic.goods()[0].price == 5000);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "sell");
  tap(logic, script.left);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "sold");
  CHECK(logic.quest().money == 5100);
  CHECK(logic.quest().garage.stock.size() == 1);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "none");
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == ModuleShopView::categories);
}

namespace {

// A shop with a gatling gun (arm 0, typed) and a beam (arm 1, untyped).
Shop armed() {
  Shop shop;
  shop.parts.arm_weapons.assign(2, WeaponRecord{});
  WeaponRecord &gun = shop.parts.arm_weapons[0];
  gun.name = "Gatling";
  gun.kind = hud_kind::machine_gun;
  gun.fire_frames = 2;
  gun.knockdown.base = 12.0F;
  WeaponRecord &beam = shop.parts.arm_weapons[1];
  beam.index = 1;
  beam.name = "Beam";
  beam.kind = hud_kind::beam;
  beam.fire_frames = 10;
  shop.table.weapon_types.arm = {"GATLING GUN"};
  return shop;
}

const ExtraStatLine *line_labelled(const std::vector<ExtraStatLine> &lines,
                                   const std::string &label) {
  for (const ExtraStatLine &line : lines) {
    if (line.label == label) {
      return &line;
    }
  }
  return nullptr;
}

} // namespace

TEST_CASE("a weapon's TYPE line comes from weapon_types.json, lettered") {
  const Shop shop = armed();
  const auto lines = extra_stats(shop.table, shop.parts, shop.garage, {GoodsKind::arm, 0, 0});
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].label == "TYPE");
  CHECK(lines[0].value == "GATLING GUN");
  CHECK(lines[0].lettered);
  // A record the table has no type for shows no line.
  CHECK(extra_stats(shop.table, shop.parts, shop.garage, {GoodsKind::arm, 1, 0}).empty());
  // The shipped table types every record of both lists.
  const WeaponTypes types = parse_weapon_types(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/weapon_types.json"));
  CHECK(types.arm.size() >= 70);
  CHECK(types.shoulder.size() >= 60);
}

TEST_CASE("a weapon's second page: the battle's values, none at 0") {
  const Shop shop = armed();
  const auto gun = detail_stats(shop.table, shop.parts, shop.garage, {GoodsKind::arm, 0, 0});
  const ExtraStatLine *rate = line_labelled(gun, "FIRE RATE");
  REQUIRE(rate != nullptr);
  // fire_frames 2: a round every other battle tick, 15 a second.
  CHECK(rate->value == "15.0");
  const ExtraStatLine *knockdown = line_labelled(gun, "KNOCKDOWN");
  REQUIRE(knockdown != nullptr);
  CHECK(knockdown->value == "12");
  CHECK(line_labelled(gun, "SPREAD") == nullptr);
  CHECK(line_labelled(gun, "BLAST DAMAGE") == nullptr);
  CHECK(line_labelled(gun, "STUN") == nullptr);
  // A beam is held, not fired shot by shot.
  const auto beam = detail_stats(shop.table, shop.parts, shop.garage, {GoodsKind::arm, 1, 0});
  CHECK(line_labelled(beam, "FIRE RATE") == nullptr);
  CHECK(detail_stats(shop.table, shop.parts, shop.garage, {GoodsKind::body, 0, 0}).empty());
}

TEST_CASE("left and right turn a weapon's details page, only a weapon's") {
  const Shop shop = armed();
  Garage owned;
  owned.add_stock({PartKind::body, 0, 0, "BODY-B", -1});
  owned.add_stock({PartKind::arm, 0, 0, "Gatling", -1});
  ModuleShop logic = selling(shop, owned);
  const ModuleShopScript &script = logic.script();
  tap(logic, script.right);
  REQUIRE(script.categories[static_cast<std::size_t>(logic.category())].kind == GoodsKind::arm);
  tap(logic, script.confirm_buttons);
  REQUIRE(logic.view() == ModuleShopView::list);
  CHECK(logic.has_details());
  CHECK(!logic.details_page());
  tap(logic, script.right);
  CHECK(logic.details_page());
  tap(logic, script.left);
  CHECK(!logic.details_page());
  tap(logic, script.right);
  CHECK(logic.view() == ModuleShopView::list);
  // Leaving and coming back starts on the first page.
  tap(logic, script.cancel_buttons);
  tap(logic, script.confirm_buttons);
  CHECK(!logic.details_page());
  // A body has no second page: right does nothing.
  tap(logic, script.cancel_buttons);
  tap(logic, script.left);
  tap(logic, script.confirm_buttons);
  REQUIRE(logic.view() == ModuleShopView::list);
  CHECK(!logic.has_details());
  tap(logic, script.right);
  CHECK(!logic.details_page());
}
