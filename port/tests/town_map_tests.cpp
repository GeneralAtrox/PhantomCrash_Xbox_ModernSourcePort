// The town map: the places, the start place, the cursor and its sounds, the
// sign, the rider, confirm and the mouse.

#include "test_support.hpp"

#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/town_map.hpp"

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

TownMapScript shipped() {
  return parse_town_map(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/town_map.json"));
}

TownMap town(const std::string &from, QuestSave quest = {}) {
  return TownMap(shipped(), from, std::move(quest));
}

void run(TownMap &map, const double seconds) {
  for (int count = 0; count < static_cast<int>(seconds * 60.0 + 0.5); ++count) {
    map.update(tick, InputFrame{0, 0, 1});
  }
}

void press(TownMap &map, const Buttons buttons) {
  map.update(tick, InputFrame{buttons, buttons, 1});
  map.update(tick, InputFrame{0, 0, 1});
}

} // namespace

TEST_CASE("the town map has its eight places") {
  const TownMapScript script = shipped();
  CHECK(script.scene == 41);
  CHECK(script.places.size() == 8U);
  CHECK(script.places[0].next == "garage");
  CHECK(script.places[5].next == "dealer_ventuno");
  CHECK(script.places[3].rider_flipped);
  CHECK(!script.places[0].rider_flipped);
}

TEST_CASE("the town map starts on the place it was opened from") {
  CHECK(town("garage").place() == 0);
  CHECK(town("club_wired").place() == 1);
  CHECK(town("event_140").place() == 2);
  CHECK(town("dealer_kojima").place() == 6);
  CHECK(town("somewhere_else").place() == 0);
}

TEST_CASE("the town map's cursor steps round the places and waits for the rider") {
  TownMap map = town("garage");
  press(map, button::left);
  CHECK(map.place() == 0); // still fading in
  run(map, 0.6);
  static_cast<void>(map.take_sounds());
  press(map, button::left);
  CHECK(map.place() == 1);
  CHECK((map.take_sounds() == std::vector<std::string>{"adjust", "town_move"}));
  CHECK(map.moving());
  press(map, button::left);
  CHECK(map.place() == 1); // the rider is still travelling
  run(map, 0.2);
  CHECK(!map.moving());
  press(map, button::right);
  run(map, 0.2);
  press(map, button::right);
  CHECK(map.place() == 7); // round past the start
}

TEST_CASE("the town map's left first goes left on screen and keeps that way round") {
  // Opened on the top, left counts up; opened on the bottom row (leaving
  // SONIC AMP among them) it counts down. Either way it never changes until
  // the map closes.
  struct Case {
    const char *from;
    int turn;
  };
  for (const Case &start :
       {Case{"garage", 1}, Case{"club_wired", 1}, Case{"wild_machines", 1}, Case{"plustech", -1},
        Case{"sonic_amp", -1}, Case{"event_132", -1}, Case{"event_153", -1},
        Case{"dealer_ventuno", -1}, Case{"dealer_kojima", 1}, Case{"dealer_american_stars", 1}}) {
    TownMap map = town(start.from);
    CHECK(map.forward_turn() == start.turn);
    const int first = map.place();
    run(map, 0.6);
    for (int step = 1; step <= 9; ++step) {
      press(map, button::left);
      CHECK(map.place() == (first + step * start.turn + 16) % 8);
      run(map, 0.2);
    }
    for (int step = 8; step >= 0; --step) {
      press(map, button::right);
      CHECK(map.place() == (first + step * start.turn + 16) % 8);
      run(map, 0.2);
    }
  }
}

TEST_CASE("left from the garage reaches Sonic Amp, and left out of it goes back to PlusTech") {
  TownMap garage = town("garage");
  run(garage, 0.6);
  for (int step = 0; step < 4; ++step) {
    press(garage, button::left);
    run(garage, 0.2);
  }
  CHECK(garage.place() == 4);
  TownMap shop = town("sonic_amp");
  run(shop, 0.6);
  press(shop, button::left);
  CHECK(shop.place() == 3); // PlusTech
  run(shop, 0.2);
  press(shop, button::right);
  run(shop, 0.2);
  press(shop, button::right);
  CHECK(shop.place() == 5); // Ventuno
}

TEST_CASE("the town map's right keeps counting down past Kojima") {
  TownMap map = town("dealer_kojima");
  REQUIRE(map.place() == 6);
  run(map, 0.6);
  press(map, button::right);
  CHECK(map.place() == 5); // Ventuno
  run(map, 0.2);
  press(map, button::right);
  CHECK(map.place() == 4); // Sonic Amp, not back to 7
  run(map, 0.2);
  press(map, button::left);
  run(map, 0.2);
  press(map, button::left);
  run(map, 0.2);
  press(map, button::left);
  CHECK(map.place() == 7);
  run(map, 0.2);
  press(map, button::left);
  CHECK(map.place() == 0); // round past the end
}

TEST_CASE("without orient_to_start the town map's left always counts up") {
  TownMapScript script = shipped();
  script.orient_to_start = false;
  TownMap map(std::move(script), "sonic_amp", {});
  REQUIRE(map.place() == 4);
  CHECK(map.forward_turn() == 1);
  run(map, 0.6);
  press(map, button::left);
  CHECK(map.place() == 5); // Ventuno, the next number
  run(map, 0.2);
  press(map, button::right);
  run(map, 0.2);
  press(map, button::right);
  CHECK(map.place() == 3); // PlusTech, the one before
}

TEST_CASE("the town map's sign grows once the map has faded in") {
  TownMap map = town("garage");
  CHECK(!map.sign());
  run(map, 0.55);
  const auto growing = map.sign();
  CHECK(growing.has_value());
  run(map, 0.3);
  const auto grown = map.sign();
  CHECK(grown.has_value());
  if (!growing || !grown || !growing->size || !grown->size) {
    return;
  }
  CHECK((*growing->size)[0] < (*grown->size)[0]);
  CHECK((*grown->size)[0] == 272.0F);
}

TEST_CASE("the town map's rider appears after the fade and faces each place's way") {
  TownMap map = town("wild_machines");
  CHECK(!map.rider());
  run(map, 0.8);
  const auto rider = map.rider();
  CHECK(rider.has_value());
  if (!rider || !rider->size) {
    return;
  }
  CHECK((*rider->size)[0] == 64.0F);
  CHECK(!rider->flip_x);
  press(map, button::left);
  run(map, 0.3);
  const auto at_plustech = map.rider();
  CHECK(map.place() == 3);
  CHECK((at_plustech && at_plustech->flip_x));
}

TEST_CASE("confirm on the town map fades out to the chosen place") {
  TownMap map = town("garage");
  run(map, 0.8);
  press(map, button::left);
  run(map, 0.3);
  static_cast<void>(map.take_sounds());
  press(map, button::a);
  CHECK((map.take_sounds() == std::vector<std::string>{"confirm"}));
  press(map, button::left);
  CHECK(map.place() == 1); // no moves while leaving
  run(map, 0.6);
  CHECK(map.done());
  CHECK(map.next() == "club_wired");
}

namespace {

void point(TownMap &map, const float x, const float y, const bool click = false) {
  InputFrame frame{0, 0, 1};
  frame.pointer = std::array<float, 2>{x, y};
  frame.pointer_moved = true;
  frame.clicked = click;
  map.update(tick, frame);
}

// The rider's pivot: a quarter of his height above his middle.
std::array<float, 2> pivot(const TownMap &map) {
  const auto rider = map.rider();
  if (!rider || !rider->size) {
    return {};
  }
  return {rider->position[0], rider->position[1] - (*rider->size)[1] * 0.25F};
}

// Circles the rider with the mouse `turns` times, clockwise.
void circle(TownMap &map, const double turns) {
  const auto [x, y] = pivot(map);
  const int steps = static_cast<int>(turns * 32.0);
  for (int step = 1; step <= steps; ++step) {
    const double angle = step * 2.0 * 3.14159265358979 / 32.0;
    point(map, x + static_cast<float>(20.0 * std::sin(angle)),
          y - static_cast<float>(20.0 * std::cos(angle)));
  }
}

} // namespace

TEST_CASE("the mouse chooses a town map place by its number or name") {
  TownMap map = town("garage");
  run(map, 0.8);
  static_cast<void>(map.take_sounds());
  point(map, 120.0F, 130.0F); // Wild Machines' number
  CHECK(map.place() == 2);
  CHECK((map.take_sounds() == std::vector<std::string>{"adjust", "town_move"}));
  run(map, 0.3);
  press(map, button::right);
  CHECK(map.place() == 1);
  run(map, 0.3);
  CHECK(map.place() == 1);    // the mouse, still, does not take it back
  point(map, 200.0F, 140.0F); // Wild Machines' name: moving over it does not choose it
  run(map, 0.3);
  CHECK(map.place() == 1);
  point(map, 20.0F, 400.0F); // nowhere
  run(map, 0.3);
  CHECK(map.place() == 1);
}

TEST_CASE("clicking the chosen town map place's sign goes there") {
  TownMap map = town("garage");
  run(map, 0.8);
  point(map, 480.0F, 190.0F, true); // the garage's sign, over Kojima's number
  CHECK(map.place() == 0);
  run(map, 0.6);
  CHECK(map.next() == "garage");
}

TEST_CASE("clicking a town map place goes there") {
  TownMap map = town("garage");
  run(map, 0.8);
  point(map, 420.0F, 340.0F, true); // Sonic Amp's name
  CHECK(map.place() == 4);
  run(map, 0.6);
  CHECK(map.done());
  CHECK(map.next() == "sonic_amp");
}

TEST_CASE("the town map's rider faces the mouse and stands up again without it") {
  TownMap map = town("club_wired");
  run(map, 0.8);
  CHECK(map.rider()->angle == 0.0F);
  const auto [x, y] = pivot(map);
  point(map, x + 30.0F, y);
  const auto turned = map.rider();
  CHECK(std::abs(turned->angle - 90.0F) < 10.0F); // he hovers a little meanwhile
  CHECK((turned->pivot && (*turned->pivot)[1] == 12.0F));
  map.update(tick, InputFrame{0, 0, 1}); // the mouse left the window
  CHECK(map.rider()->angle == 0.0F);
}

TEST_CASE("each full turn of the town map's rider pays 10 NY, up to 100") {
  QuestSave quest;
  quest.money = 500;
  quest.spin_paid = 80;
  TownMap map = town("club_wired", quest);
  run(map, 0.8);
  static_cast<void>(map.take_sounds());
  circle(map, 1.2);
  CHECK(map.quest().money == 510);
  CHECK(map.quest().spin_paid == 90);
  CHECK(map.quest_changed());
  CHECK((map.take_sounds() == std::vector<std::string>{"spin_pay"}));
  circle(map, 3.5);
  CHECK(map.quest().money == 520);
  CHECK(map.quest().spin_paid == 100);
}

TEST_CASE("the town map's spin stops paying at the money limit") {
  QuestSave quest;
  quest.money = money_limit;
  TownMap map = town("club_wired", quest);
  run(map, 0.8);
  static_cast<void>(map.take_sounds());
  circle(map, 1.2);
  CHECK(map.quest().money == money_limit);
  CHECK(map.quest().spin_paid == 10);
  CHECK(map.take_sounds().empty());
}
