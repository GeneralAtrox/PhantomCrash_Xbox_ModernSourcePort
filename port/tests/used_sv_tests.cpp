// Wild Machines' used SVs: the lineup and its maker gate, buying one into a
// hangar and boarding it, and leaving for the Wild Machines menu.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/used_sv.hpp"

#include <string>
#include <vector>

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

UsedSvScript shipped_script() {
  return parse_used_sv(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/used_sv.json"));
}

struct Tables {
  PartTables parts;
  GarageTables garage;
};

RobotRecord robot_on_body(const int body) {
  RobotRecord robot;
  robot.name = "NOSUREHL";
  robot.parts[static_cast<std::size_t>(PartSlot::body)] = RobotPart{body, 1};
  robot.parts[static_cast<std::size_t>(PartSlot::legs)] = RobotPart{0, 1};
  robot.parts[static_cast<std::size_t>(PartSlot::chip)] = RobotPart{0, 5};
  return robot;
}

Tables tables() {
  Tables out;
  out.parts.chips.push_back(ChipRecord{});
  out.parts.chips[0].name = "OCG-2000";
  out.parts.bodies.assign(2, ArmourRecord{});
  out.parts.bodies[1].maker = 3; // Tolomeo
  out.garage.robots = {robot_on_body(0), robot_on_body(1)};
  out.garage.used_sv_lineups = {{0, 1, 9}};
  return out;
}

SvOffer offer() {
  return {robot_on_body(0), -1, 5000, 0};
}

void settle(UsedSv &logic, const double seconds = 1.5) {
  for (double time = 0.0; time < seconds; time += tick) {
    logic.update(tick, InputFrame{});
  }
}

void tap(UsedSv &logic, const Buttons buttons) {
  logic.update(tick, InputFrame{buttons, buttons, 1});
  settle(logic, 0.3);
}

UsedSv opened(const Tables &shared, const std::int64_t money) {
  QuestSave quest;
  quest.money = money;
  UsedSv logic(shipped_script(), {offer()}, quest, shared.parts, shared.garage);
  settle(logic);
  return logic;
}

} // namespace

TEST_CASE("used_sv.json has six slots and EXIT, the purchase and the CHIP naming") {
  const UsedSvScript script = shipped_script();
  CHECK(script.slots.size() == 7);
  CHECK(script.badges.size() == 6);
  CHECK(script.purchase.actions.size() == 3);
  CHECK(script.purchase.messages.count("delivered") == 1);
  CHECK(script.purchase.messages.count("name_chip") == 1);
  CHECK(script.exit_to == "wild_machines");
}

TEST_CASE("a gated maker's SV shows once its contest is won") {
  const Tables shared = tables();
  const UsedSvScript script = shipped_script();
  const auto locked = used_sv_offers(script, shared.garage, shared.parts, 0, {});
  CHECK(locked.size() == 1); // robot 9 does not exist; robot 1 is Tolomeo's
  CHECK(locked[0].maker == 0);
  const auto open = used_sv_offers(script, shared.garage, shared.parts, 0, {script.unlock_contest});
  CHECK(open.size() == 2);
  CHECK(open[1].maker == 3);
  CHECK(used_sv_offers(script, shared.garage, shared.parts, 4, {}).empty());
}

TEST_CASE("fewer SVs sit in the last slots, and cancel jumps to EXIT") {
  const Tables shared = tables();
  UsedSv logic = opened(shared, 0);
  CHECK(logic.opened());
  CHECK(logic.item_count() == 2);
  CHECK(logic.slot_of(0) == 5);
  CHECK(logic.slot_of(1) == 6);
  CHECK(logic.info_open() == 1.0F);
  tap(logic, logic.script().cancel_buttons);
  CHECK(logic.cursor() == 1);
  CHECK(logic.info_open() == 0.0F);
  tap(logic, logic.script().confirm_buttons);
  settle(logic, 3.0);
  CHECK(logic.done());
  CHECK(logic.next() == "wild_machines");
}

TEST_CASE("buying a used SV fits it in a hangar, boards it and names its CHIP") {
  const Tables shared = tables();
  UsedSv logic = opened(shared, 8000);
  const UsedSvScript &script = logic.script();
  tap(logic, script.confirm_buttons);
  CHECK(logic.purchase().view() == PurchaseView::trade);
  tap(logic, script.purchase.right);
  tap(logic, script.confirm_buttons);
  CHECK(logic.purchase().view() == PurchaseView::hangars);
  tap(logic, script.confirm_buttons);
  CHECK(logic.purchase().message() == "delivered");
  CHECK(logic.quest().money == 3000);
  const Hangar &bay = logic.quest().garage.hangars[0];
  CHECK(bay.name == "NOSUREHL");
  CHECK(logic.quest().garage.stock.at(bay.part(PartSlot::chip)).name == "OCG-2000");
  tap(logic, script.confirm_buttons);
  CHECK(logic.purchase().view() == PurchaseView::question);
  tap(logic, script.purchase.left);
  tap(logic, script.confirm_buttons);
  CHECK(logic.purchase().message() == "boarded");
  CHECK(logic.quest().garage.riding == 0);
  tap(logic, script.confirm_buttons);
  CHECK(logic.purchase().message() == "name_chip");
  tap(logic, script.confirm_buttons);
  settle(logic, 3.0);
  CHECK(logic.done());
  CHECK(logic.next() == "name_entry");
  CHECK(logic.naming_stock() == bay.part(PartSlot::chip));
}

TEST_CASE("without the money Wild Machines says so") {
  const Tables shared = tables();
  UsedSv logic = opened(shared, 100);
  const UsedSvScript &script = logic.script();
  tap(logic, script.confirm_buttons);
  tap(logic, script.purchase.right);
  tap(logic, script.confirm_buttons);
  CHECK(logic.purchase().message() == "poor");
  tap(logic, script.confirm_buttons);
  CHECK(!logic.purchase().open());
  CHECK(logic.quest().money == 100);
}
