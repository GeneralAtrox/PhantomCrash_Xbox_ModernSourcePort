// The SV dealers: the menu, buying an SV into a hangar, boarding it and
// naming its CHIP, and the notices when it cannot be bought.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/dealer.hpp"

#include <string>
#include <vector>

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

DealerScript shipped_script() {
  return parse_dealer(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/dealer.json"));
}

struct Tables {
  PartTables parts;
  GarageTables garage;
};

Tables tables() {
  Tables out;
  out.parts.chips.push_back(ChipRecord{});
  out.parts.chips[0].name = "OCG-2000";
  out.garage.modules.push_back({"TURBO", 1.0F, 5000});
  return out;
}

DealerOffer offer() {
  RobotRecord robot;
  robot.name = "HL-220H";
  robot.parts[static_cast<std::size_t>(PartSlot::body)] = RobotPart{0, 1};
  robot.parts[static_cast<std::size_t>(PartSlot::legs)] = RobotPart{0, 1};
  robot.parts[static_cast<std::size_t>(PartSlot::chip)] = RobotPart{0, 99};
  robot.modules = {0};
  return {robot, 0, 5000};
}

QuestSave quest_with(const std::int64_t money) {
  QuestSave quest;
  quest.money = money;
  return quest;
}

void settle(Dealer &logic, const double seconds = 1.5) {
  for (double time = 0.0; time < seconds; time += tick) {
    logic.update(tick, InputFrame{});
  }
}

void tap(Dealer &logic, const Buttons buttons) {
  logic.update(tick, InputFrame{buttons, buttons, 1});
  settle(logic, 0.3);
}

// The shipped script, one offer, the menu open on it and the trade window
// on BUY.
Dealer at_buy(const Tables &shared, const std::int64_t money) {
  const DealerScript script = shipped_script();
  Dealer logic(script, "dealer_ventuno", {offer()}, quest_with(money), shared.parts, shared.garage,
               "town_map");
  settle(logic);
  tap(logic, script.right);
  tap(logic, script.confirm_buttons);
  tap(logic, script.right);
  return logic;
}

} // namespace

TEST_CASE("dealer.json has the menu, the trade window and the three dealers") {
  const DealerScript script = shipped_script();
  CHECK(script.items.size() == 5);
  CHECK(script.items.front().id == "profile");
  CHECK(script.items.back().id == "exit");
  CHECK(script.purchase.actions.size() == 3);
  CHECK(script.purchase.hangars.numbers.size() == garage_hangar_count);
  CHECK(script.dealers.size() == 3);
  CHECK(script.dealers.at("dealer_american_stars").index == 0);
  CHECK(script.dealers.at("dealer_kojima").index == 1);
  CHECK(script.dealers.at("dealer_ventuno").index == 2);
  CHECK(script.purchase.messages.count("delivered") == 1);
  CHECK(script.purchase.messages.count("board") == 1);
}

TEST_CASE("the dealer opens on the profile, and cancel jumps to EXIT") {
  const Tables shared = tables();
  const DealerScript script = shipped_script();
  Dealer logic(script, "dealer_kojima", {offer()}, quest_with(0), shared.parts, shared.garage,
               "town_map");
  settle(logic);
  CHECK(logic.band_open() == 1.0F);
  CHECK(logic.menu_open() == 1.0F);
  CHECK(logic.cursor() == 0);
  CHECK(logic.chosen_offer() == -1);
  tap(logic, script.right);
  CHECK(logic.chosen_offer() == 0);
  tap(logic, script.right);
  CHECK(logic.chosen_offer() == -1); // only one SV today
  tap(logic, script.cancel_buttons);
  CHECK(logic.cursor() == 4);
  tap(logic, script.confirm_buttons);
  settle(logic, 3.0);
  CHECK(logic.done());
  CHECK(logic.next() == "town_map");
}

TEST_CASE("buying an SV fits it in a hangar, boards it and names its CHIP") {
  const Tables shared = tables();
  const DealerScript script = shipped_script();
  Dealer logic = at_buy(shared, 8000);
  CHECK(logic.view() == DealerView::trade);
  CHECK(logic.action() == 1);
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == DealerView::hangars);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "delivered");
  CHECK(logic.quest().money == 3000);
  CHECK(logic.quest().garage.hangars[0].name == "HL-220H");
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == DealerView::question);
  CHECK(logic.choice() == 1);
  tap(logic, script.left);
  CHECK(logic.choice() == 0);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "boarded");
  CHECK(logic.quest().garage.riding == 0);
  CHECK(logic.quest().riding == "HL-220H");
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "name_chip");
  tap(logic, script.confirm_buttons);
  settle(logic, 3.0);
  CHECK(logic.done());
  CHECK(logic.next() == "name_entry");
  const int chip = logic.naming_stock();
  CHECK(chip >= 0);
  CHECK(logic.quest().garage.stock.at(chip).kind == PartKind::chip);
}

TEST_CASE("a hangar that is not the player's says so") {
  const Tables shared = tables();
  const DealerScript script = shipped_script();
  Dealer logic = at_buy(shared, 8000);
  tap(logic, script.confirm_buttons);
  tap(logic, script.left); // last hangar, not owned
  settle(logic, 0.5);
  CHECK(logic.hangar() == garage_hangar_count - 1);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "not_yours");
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == DealerView::hangars);
}

TEST_CASE("the purchase carousel wraps to hangar twelve and accepts its second digit") {
  const Tables shared = tables();
  const auto script = shipped_script().purchase;
  QuestSave quest = quest_with(8000);
  quest.garage = new_game_garage(garage_hangar_count);
  SvPurchase purchase(script, shared.parts, shared.garage);
  purchase.start(offer());
  (void)purchase.update(0.5, InputFrame{}, quest);
  (void)purchase.update(tick, InputFrame{script.right, script.right, 1}, quest);
  (void)purchase.update(0.5, InputFrame{}, quest);
  (void)purchase.update(tick, InputFrame{script.confirm, script.confirm, 1}, quest);
  REQUIRE(purchase.view() == PurchaseView::hangars);
  (void)purchase.update(tick, InputFrame{script.left, script.left, 1}, quest);
  (void)purchase.update(0.5, InputFrame{}, quest);
  REQUIRE(purchase.hangar() == 11);
  InputFrame click;
  click.clicked = true;
  click.pointer =
      std::array<float, 2>{script.hangars.centre[0] + 50, script.hangars.centre[1] + 15};
  (void)purchase.update(tick, click, quest);
  CHECK(purchase.message() == "delivered");
  CHECK(quest.garage.hangars[11].complete());
  CHECK(quest.garage.hangars[11].name == "HL-220H");
  CHECK(quest.money == 3000);
}

TEST_CASE("without the money the dealer says so") {
  const Tables shared = tables();
  const DealerScript script = shipped_script();
  Dealer logic = at_buy(shared, 100);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "poor");
  CHECK(logic.quest().money == 100);
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == DealerView::menu);
}

TEST_CASE("with every hangar full there is no hangar for it") {
  const Tables shared = tables();
  const DealerScript script = shipped_script();
  const DealerScript copy = script;
  QuestSave quest = quest_with(99999);
  for (int hangar = 0; hangar < garage_new_game_hangars; ++hangar) {
    install_robot(quest.garage, hangar, offer().robot, shared.parts, shared.garage);
  }
  Dealer logic(copy, "dealer_ventuno", {offer()}, quest, shared.parts, shared.garage, "town_map");
  settle(logic);
  tap(logic, script.right);
  tap(logic, script.confirm_buttons);
  tap(logic, script.right);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "no_hangar");
}

TEST_CASE("a dealer's offers come from its lineup, priced by their parts and the markup") {
  Tables shared = tables();
  RobotRecord robot = offer().robot;
  shared.garage.robots = {robot, robot};
  shared.garage.dealer_lineups.push_back({LineupEntry{1, 4}, LineupEntry{0, 5}, LineupEntry{7, 6}});
  const auto offers = dealer_offers(shared.garage, shared.parts, 0, 1.1F);
  CHECK(offers.size() == 2); // robot 7 does not exist
  CHECK(offers[0].picture == 4);
  CHECK(offers[0].price == 5500); // parts worth 5000, plus 10%
  CHECK(dealer_offers(shared.garage, shared.parts, 0, 1.0F)[0].price == 5000);
  CHECK(dealer_offers(shared.garage, shared.parts, 3, 1.1F).empty());
}
