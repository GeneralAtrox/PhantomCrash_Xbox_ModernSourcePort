// The garage's HANGAR: the wheel of cards, buying a hangar, a hangar's
// menu, boarding a Scoobee and what its card says about it.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/hangar_menu.hpp"

#include <algorithm>
#include <string>

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

HangarMenuScript shipped_script() {
  return parse_hangar_menu(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/hangar.json"));
}

// A body of maker 2 heavier than its legs carry, the legs and a CHIP.
PartTables tables() {
  PartTables parts;
  parts.bodies.assign(1, ArmourRecord{});
  parts.bodies[0].maker = 2;
  parts.bodies[0].weight = {900.0F, 0.0F, 9900.0F, 0, 1};
  parts.legs.assign(1, ArmourRecord{});
  parts.legs[0].weight = {100.0F, 0.0F, 9900.0F, 0, 1};
  parts.legs[0].capacity = {500.0F, 0.0F, 9900.0F, 0, 1};
  parts.chips.assign(1, ChipRecord{});
  return parts;
}

// Hangar 0 holds a complete robot; hangars 0 to 2 are owned.
QuestSave owned(const std::int64_t money) {
  QuestSave quest;
  quest.money = money;
  quest.garage = new_game_garage(garage_new_game_hangars);
  Garage &garage = quest.garage;
  Hangar &bay = garage.hangars[0];
  bay.name = "ALPHA";
  bay.parts[static_cast<std::size_t>(PartSlot::body)] =
      garage.add_stock({PartKind::body, 0, 0, "", 0});
  bay.parts[static_cast<std::size_t>(PartSlot::legs)] =
      garage.add_stock({PartKind::legs, 0, 0, "", 0});
  bay.parts[static_cast<std::size_t>(PartSlot::chip)] =
      garage.add_stock({PartKind::chip, 0, 0, "", 0});
  return quest;
}

void settle(HangarMenu &logic, const double seconds = 1.5) {
  for (double time = 0.0; time < seconds; time += tick) {
    logic.update(tick, InputFrame{});
  }
}

void tap(HangarMenu &logic, const Buttons buttons, const int times = 1) {
  for (int count = 0; count < times; ++count) {
    logic.update(tick, InputFrame{buttons, buttons, 1});
    settle(logic, 0.5);
  }
}

struct Opened {
  PartTables parts = tables();
  HangarMenu logic;
  explicit Opened(const std::int64_t money) : logic(shipped_script(), owned(money), parts) {
    settle(logic);
  }
};

} // namespace

TEST_CASE("hangar.json reads the wheel, the plates, the menu and the routes") {
  const HangarMenuScript script = shipped_script();
  CHECK(script.scene == 14);
  CHECK(script.cards.numbers.size() == garage_hangar_count);
  CHECK(script.plates.plates.size() == garage_hangar_count);
  REQUIRE(script.menu.items.size() == 5);
  CHECK(script.menu.items[3].action == HangarAction::assemble);
  CHECK(script.routes.at(HangarAction::spec) == "sv_spec");
  CHECK(script.hangar_price == 10000);
  CHECK(script.messages.count("ridden") == 1);
  // The Scoobees stand 7 apart, one to a card, two either side (0x01d8ba).
  CHECK(script.robots.spacing == 7.0F);
  CHECK(script.robots.neighbours == 2);
}

TEST_CASE("a hangar's card says what keeps it from a battle") {
  const PartTables parts = tables();
  const QuestSave quest = owned(0);
  const HangarStatus full = hangar_status(parts, quest.garage, 0, 0.05F);
  CHECK(full.owned);
  CHECK(full.complete);
  CHECK(full.unarmed);
  CHECK(full.overweight); // 1000 against a capacity of 500
  CHECK(full.maker == 2);
  const HangarStatus bare = hangar_status(parts, quest.garage, 1, 0.05F);
  CHECK(bare.empty);
  CHECK(!bare.complete);
  CHECK(!hangar_status(parts, quest.garage, 5, 0.05F).owned);
}

TEST_CASE("the wheel wraps, scrolls the chosen card to the centre, cancel goes to EXIT") {
  Opened shop(0);
  HangarMenu &logic = shop.logic;
  const HangarMenuScript &script = logic.script();
  CHECK(logic.card_x(0) == script.cards.centre_x);
  CHECK(logic.card_x(1) == script.cards.centre_x + script.cards.step);
  CHECK(logic.card_x(HangarMenu::exit_card) == script.cards.centre_x - script.cards.step);
  tap(logic, script.left);
  CHECK(logic.card() == HangarMenu::exit_card);
  tap(logic, script.right, 2);
  CHECK(logic.card() == 1);
  CHECK(logic.card_x(1) == script.cards.centre_x);
  tap(logic, script.cancel_buttons);
  CHECK(logic.card() == HangarMenu::exit_card);
  tap(logic, script.confirm_buttons);
  settle(logic, 2.0);
  CHECK(logic.done());
  CHECK(logic.next() == "garage");
}

TEST_CASE("hangars eleven and twelve can be selected, purchased and reached by pointer") {
  Opened shop(20000);
  auto &logic = shop.logic;
  const auto &script = logic.script();
  for (int index = 10; index < garage_hangar_count; ++index) {
    InputFrame click;
    click.clicked = true;
    click.pointer = std::array<float, 2>{script.plates.to_x[index] + 25, script.plates.y + 12};
    logic.update(tick, click);
    settle(logic, 0.5);
    CHECK(logic.card() == index);
    CHECK(logic.card_x(index) == script.cards.centre_x);
    tap(logic, script.confirm_buttons);
    REQUIRE(logic.view() == HangarView::buy);
    tap(logic, script.left);
    tap(logic, script.confirm_buttons);
    CHECK(logic.quest().garage.hangars[index].owned);
    tap(logic, script.confirm_buttons);
  }
  CHECK(logic.quest().money == 0);
  tap(logic, script.right);
  CHECK(logic.card() == HangarMenu::exit_card);
  tap(logic, script.right);
  CHECK(logic.card() == 0);
}

TEST_CASE("a hangar for sale is bought for its price, or the money will not do") {
  Opened rich(20000);
  HangarMenu &logic = rich.logic;
  const HangarMenuScript &script = logic.script();
  tap(logic, script.right, 3);
  tap(logic, script.confirm_buttons);
  REQUIRE(logic.view() == HangarView::buy);
  CHECK(logic.choice() == 1);
  tap(logic, script.left);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "thanks");
  CHECK(logic.quest().money == 10000);
  CHECK(logic.quest().garage.hangars[3].owned);

  Opened poor(100);
  tap(poor.logic, script.right, 3);
  tap(poor.logic, script.confirm_buttons);
  tap(poor.logic, script.left);
  tap(poor.logic, script.confirm_buttons);
  CHECK(poor.logic.message() == "poor");
  CHECK(!poor.logic.quest().garage.hangars[3].owned);
}

TEST_CASE("RIDE boards a complete hangar; TEST DRIVE waits for the ridden one") {
  Opened shop(0);
  HangarMenu &logic = shop.logic;
  const HangarMenuScript &script = logic.script();
  tap(logic, script.confirm_buttons);
  REQUIRE(logic.view() == HangarView::menu);
  CHECK(logic.menu_item() == 0);
  CHECK(!logic.can_choose(HangarAction::test_drive));
  tap(logic, script.down);
  CHECK(logic.menu_item() == 2); // over TEST DRIVE
  tap(logic, script.up);
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "boarded");
  // Boarded, with the board sound, once the notice is dismissed (0x01e2d1).
  CHECK(logic.quest().garage.riding != 0);
  (void)logic.take_sounds();
  tap(logic, script.confirm_buttons);
  const auto sounds = logic.take_sounds();
  CHECK(std::ranges::find(sounds, script.board_sound) != sounds.end());
  CHECK(logic.quest().garage.riding == 0);
  CHECK(logic.quest().riding == "ALPHA");
  CHECK(logic.view() == HangarView::wheel);
  tap(logic, script.confirm_buttons);
  CHECK(logic.can_choose(HangarAction::test_drive));
  tap(logic, script.confirm_buttons);
  CHECK(logic.message() == "ridden");
  tap(logic, script.confirm_buttons);
  CHECK(logic.view() == HangarView::menu);
  tap(logic, script.cancel_buttons);
  CHECK(logic.menu_item() == 4);
  tap(logic, script.up);
  tap(logic, script.confirm_buttons);
  settle(logic, 2.0);
  CHECK(logic.next() == "assemble");
}

TEST_CASE("an incomplete hangar offers only ASSEMBLE and CLOSE; SPEC reopens its menu") {
  Opened shop(0);
  HangarMenu &logic = shop.logic;
  const HangarMenuScript &script = logic.script();
  tap(logic, script.right);
  tap(logic, script.confirm_buttons);
  REQUIRE(logic.view() == HangarView::menu);
  CHECK(logic.menu_item() == 3);
  tap(logic, script.up);
  CHECK(logic.menu_item() == 3);
  HangarMenu back(script, owned(0), shop.parts, 0, HangarAction::spec);
  CHECK(back.menu_open());
  CHECK(back.menu_item() == 2);
}

TEST_CASE("from KEEP SV the hangar takes only an armed, complete Scoobee and KEEP leaves with it") {
  const PartTables parts = tables();
  const HangarMenuScript script = hangar_from(shipped_script(), "keep_sv");
  CHECK(script.cards.exit.id == "1525");
  REQUIRE(script.menu.items.size() == 3);
  CHECK(script.menu.items.front().action == HangarAction::keep);
  CHECK(hangar_from(shipped_script(), "rumbling").cards.exit.id == "1524");
  CHECK(!hangar_from(shipped_script(), "rumbling").refuse);
  QuestSave quest = owned(0);
  HangarMenu unarmed(script, quest, parts);
  settle(unarmed);
  tap(unarmed, script.confirm_buttons);
  CHECK(unarmed.message() == "keep_unarmed");
  tap(unarmed, script.confirm_buttons);
  tap(unarmed, script.right);
  tap(unarmed, script.confirm_buttons);
  CHECK(unarmed.message() == "keep_board");
  Garage &garage = quest.garage;
  garage.hangars[0].parts[static_cast<std::size_t>(PartSlot::right_arm)] =
      garage.add_stock({PartKind::arm, 0, 0, "", 0});
  HangarMenu armed(script, quest, parts);
  settle(armed);
  tap(armed, script.confirm_buttons);
  REQUIRE(armed.view() == HangarView::menu);
  CHECK(armed.menu_item() == 0);
  tap(armed, script.confirm_buttons);
  settle(armed, 2.0);
  CHECK(armed.next() == "keep_sv");
  CHECK(armed.kept_hangar() == 0);
}
