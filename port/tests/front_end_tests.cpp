// Title and main menu logic, sprite placement and the draw-flag animations.

#include "test_support.hpp"

#include <array>
#include <string>
#include <vector>

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/front_end.hpp"

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

FrontEnd shipped() {
  const auto roots = DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT);
  return FrontEnd(parse_title(roots.document("ui/title.json")),
                  parse_main_menu(roots.document("ui/main_menu.json")));
}

InputFrame press(Buttons buttons, int controllers = 1) {
  return InputFrame{buttons, buttons, controllers};
}

InputFrame idle(int controllers = 1) {
  return InputFrame{0, 0, controllers};
}

void run(FrontEnd &front_end, double seconds, const InputFrame &input = idle()) {
  for (int count = 0; count < static_cast<int>(seconds * 60.0 + 0.5); ++count) {
    front_end.update(tick, input);
  }
}

// Presses once, and returns the sounds that press made.
std::vector<std::string> tap(FrontEnd &front_end, Buttons buttons, int controllers = 1) {
  front_end.update(tick, press(buttons, controllers));
  return front_end.take_sounds();
}

std::string item_id(const FrontEnd &front_end) {
  return front_end.menu()->items[static_cast<std::size_t>(front_end.cursor())].id;
}

} // namespace

TEST_CASE("the title fades in, then START opens the menu with its sound") {
  auto front_end = shipped();
  CHECK(front_end.mode() == FrontEnd::Mode::title);
  CHECK(front_end.black_alpha() == 1.0F);
  CHECK(front_end.music() == "title");
  // START is ignored until the fade-in ends.
  CHECK(tap(front_end, button::start).empty());
  run(front_end, 0.5);
  CHECK(front_end.black_alpha() == 0.0F);
  // A is not START on the title.
  CHECK(tap(front_end, button::a).empty());
  CHECK(tap(front_end, button::start) == std::vector<std::string>{"title_start"});
  CHECK(front_end.mode() == FrontEnd::Mode::menu);
  CHECK(front_end.black_alpha() == 0.0F);
  CHECK(item_id(front_end) == "quest_mode");
  CHECK(front_end.music() == "title");
}

TEST_CASE("the title fades out to the attract demo after 30 s") {
  auto front_end = shipped();
  run(front_end, 0.5 + 29.9);
  CHECK(!front_end.done());
  CHECK(front_end.black_alpha() == 0.0F);
  run(front_end, 0.2);
  CHECK(front_end.black_alpha() > 0.0F);
  run(front_end, 0.5);
  CHECK(front_end.done());
  CHECK(front_end.next() == "attract_demo");
}

TEST_CASE("the cursor wraps on a press and stops at the ends when held") {
  auto front_end = shipped();
  run(front_end, 0.5);
  tap(front_end, button::start);
  CHECK(tap(front_end, button::up) == std::vector<std::string>{"cursor"});
  CHECK(item_id(front_end) == "option");
  tap(front_end, button::down);
  CHECK(item_id(front_end) == "quest_mode");
  // Holding down: one step now, a repeat after 0.4 s, then clamped at the end.
  tap(front_end, button::down);
  CHECK(item_id(front_end) == "vs_mode");
  run(front_end, 1.0, InputFrame{button::down, 0, 1});
  CHECK(item_id(front_end) == "option");
  CHECK(front_end.take_sounds() == std::vector<std::string>{"cursor"});
}

TEST_CASE("VS needs two controllers, then fades out to its screen") {
  auto front_end = shipped();
  run(front_end, 0.5);
  tap(front_end, button::start);
  tap(front_end, button::down);
  CHECK(tap(front_end, button::a) == std::vector<std::string>{"cancel"});
  CHECK(front_end.mode() == FrontEnd::Mode::menu);
  CHECK(!front_end.available(front_end.menu()->items[1]));
  CHECK(tap(front_end, button::start, 2) == std::vector<std::string>{"confirm"});
  // Input is ignored while fading out.
  CHECK(tap(front_end, button::b, 2).empty());
  run(front_end, 0.5, idle(2));
  CHECK(front_end.done());
  CHECK(front_end.next() == "vs_mode");
}

TEST_CASE("quest mode opens its submenu, and B steps back out") {
  auto front_end = shipped();
  run(front_end, 0.5);
  tap(front_end, button::start);
  CHECK(tap(front_end, button::a) == std::vector<std::string>{"confirm"});
  CHECK(front_end.menu()->hints.size() == 2);
  CHECK(item_id(front_end) == "new_game");
  tap(front_end, button::down);
  CHECK(item_id(front_end) == "load_game");
  CHECK(tap(front_end, button::b) == std::vector<std::string>{"cancel"});
  CHECK(front_end.mode() == FrontEnd::Mode::menu);
  CHECK(item_id(front_end) == "quest_mode");
  CHECK(tap(front_end, button::back) == std::vector<std::string>{"cancel"});
  CHECK(front_end.mode() == FrontEnd::Mode::title);
  CHECK(front_end.black_alpha() == 0.0F);
  tap(front_end, button::start);
  tap(front_end, button::a);
  tap(front_end, button::a);
  run(front_end, 0.5);
  CHECK(front_end.next() == "new_game");
}

TEST_CASE("the mouse opens the title, moves the cursor by hovering and confirms by clicking") {
  auto front_end = shipped();
  run(front_end, 0.5);
  InputFrame click = idle();
  click.pointer = std::array<float, 2>{10.0F, 10.0F};
  click.clicked = true;
  front_end.update(tick, click);
  CHECK(front_end.mode() == FrontEnd::Mode::menu);
  static_cast<void>(front_end.take_sounds());
  // Hovering moves the cursor; a pointer resting on an item does not.
  InputFrame hover = idle();
  hover.pointer = std::array<float, 2>{0.0F, 0.0F};
  hover.pointer_moved = true;
  front_end.update(tick, hover, 2);
  CHECK(item_id(front_end) == "option");
  CHECK(front_end.take_sounds() == std::vector<std::string>{"cursor"});
  hover.pointer_moved = false;
  front_end.update(tick, hover, 0);
  CHECK(item_id(front_end) == "option");
  // A click off every item does nothing; on one it confirms.
  front_end.update(tick, click);
  CHECK(item_id(front_end) == "option");
  CHECK(front_end.take_sounds().empty());
  front_end.update(tick, click, 0);
  CHECK(front_end.menu()->items.size() == 2); // the quest submenu
  front_end.update(tick, click, 1);
  run(front_end, 1.0);
  CHECK(front_end.done());
  CHECK(front_end.next() == "load_game");
}

TEST_CASE("the menu returns to the title 75 s after opening") {
  auto front_end = shipped();
  run(front_end, 0.5);
  tap(front_end, button::start);
  // Moving the cursor does not reset the timer (as in the original).
  run(front_end, 40.0);
  CHECK(tap(front_end, button::down) == std::vector<std::string>{"cursor"});
  run(front_end, 34.9);
  CHECK(front_end.mode() == FrontEnd::Mode::menu);
  run(front_end, 0.2);
  CHECK(front_end.mode() == FrontEnd::Mode::title);
  // The timeout is silent; only B plays the cancel sound.
  CHECK(front_end.take_sounds().empty());
}

TEST_CASE("returning from a screen puts the cursor on the item that led there") {
  auto front_end = shipped();
  front_end.open_menu_from("option");
  CHECK(front_end.mode() == FrontEnd::Mode::menu);
  CHECK(front_end.black_alpha() == 1.0F);
  CHECK(item_id(front_end) == "option");
  front_end.open_menu_from("load_game");
  CHECK(item_id(front_end) == "quest_mode");
}

TEST_CASE("draw-flag animations follow the original's 80-frame wave") {
  CHECK(animation_alpha(Animation::none, 5.0) == 1.0F);
  CHECK(animation_alpha(Animation::fade, 0.0) == 1.0F);
  CHECK(animation_alpha(Animation::fade, 40.0 / 60.0) < 0.01F);
  CHECK(animation_alpha(Animation::pulse, 40.0 / 60.0) > 0.29F);
  CHECK(animation_alpha(Animation::pulse, 40.0 / 60.0) < 0.31F);
  // Blink: on for frames 0-19 and 61-80, off in between.
  CHECK(animation_alpha(Animation::blink, 10.0 / 60.0) == 1.0F);
  CHECK(animation_alpha(Animation::blink, 40.0 / 60.0) == 0.0F);
  CHECK(animation_alpha(Animation::blink, 70.0 / 60.0) == 1.0F);
  CHECK(animation_alpha(Animation::blink, (81.0 + 10.0) / 60.0) == 1.0F);
}

TEST_CASE("repeated sprites scroll and wrap like the title scanlines") {
  const auto sprite = parse_sprite(json::parse(R"({
    "image": "a.png", "rect": [0, 848, 640, 872], "position": [320, 0], "centered": true,
    "repeat": {"count": 20, "step": [0, 24], "wrap": 480, "scroll_per_second": [0, 12],
               "scroll_reset": 640}})"));
  auto quads = place_sprite(sprite, 0.0);
  CHECK(quads.size() == 20);
  CHECK(quads[0].x == 0.0F && quads[0].y == -12.0F && quads[0].width == 640.0F);
  CHECK(quads[19].y == 456.0F - 12.0F);
  // 10 s scrolls 120 units; the last strip wraps to the top.
  quads = place_sprite(sprite, 10.0);
  CHECK(quads[0].y > 107.9F && quads[0].y < 108.1F);
  CHECK(quads[19].y > 83.9F && quads[19].y < 84.1F);
  // The offset restarts at 640 units (53.3 s).
  quads = place_sprite(sprite, 640.0 / 12.0 + 1.0);
  CHECK(quads[0].y > -0.1F && quads[0].y < 0.1F);
  const auto band = parse_sprite(json::parse(
      R"({"image": "a.png", "rect": [1016, 0, 1024, 40], "position": [0, 427], "size": [640, 54]})"));
  CHECK(place_sprite(band, 0.0)[0].height == 54.0F);
  CHECK(band.layer == 0x101);
}

TEST_CASE("front-end parsing names the entry at fault") {
  CHECK_THROWS(parse_colour("404040"));
  CHECK(parse_colour("#404040") == 0x404040);
  CHECK_THROWS(parse_sprite(json::parse(R"({"image": "a.png", "rect": [0, 0, 1]})")));
  CHECK_THROWS(parse_sprite(
      json::parse(R"({"image": "a.png", "rect": [0, 0, 1, 1], "animation": "spin"})")));
  CHECK_THROWS(parse_main_menu(json::parse(R"({"items": []})")));
  CHECK_THROWS(parse_title(json::parse(R"({"layers": []})")));
}
