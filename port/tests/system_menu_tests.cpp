// The garage's system menu (an OPTION-style list) and the menus it opens,
// which return to it.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/load_game.hpp"
#include "phantomcrash/option_menu.hpp"

#include <string>

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

json::JsonValue shipped_document(const char *relative) {
  return DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document(relative);
}

OptionScript system_script() {
  return parse_option(shipped_document("ui/system.json"), "system.json");
}

void settle(OptionMenu &menu, const double seconds = 0.6) {
  for (double time = 0.0; time < seconds; time += tick) {
    menu.update(tick, InputFrame{});
  }
}

void tap(OptionMenu &menu, const Buttons buttons) {
  menu.update(tick, InputFrame{buttons, buttons, 1});
  menu.update(tick, InputFrame{});
}

} // namespace

TEST_CASE("the system menu shows the day and money and returns to the garage") {
  const OptionScript script = system_script();
  CHECK(script.items.size() == 6); // the original's five and the port's QUIT
  CHECK(script.hud);
  CHECK(script.exit_to == "garage");
  CHECK(script.riding_plate_y.has_value());
}

TEST_CASE("back from the OPTION screen the system menu's cursor is on OPTION") {
  OptionMenu menu(system_script(), {}, {}, "option");
  CHECK(menu.script().items[static_cast<std::size_t>(menu.cursor())].id == "option");
}

TEST_CASE("cancel moves the system menu's cursor to EXIT, and LOAD leads to LOAD GAME") {
  OptionMenu menu(system_script(), {}, {});
  settle(menu);
  tap(menu, button::b);
  CHECK(menu.script().items[static_cast<std::size_t>(menu.cursor())].id == "exit");
  CHECK(!menu.done());
  tap(menu, button::down);
  tap(menu, button::down);
  tap(menu, button::a);
  settle(menu);
  CHECK(menu.done());
  CHECK(menu.next() == "load_game");
}

TEST_CASE("OPTION and LOAD GAME entered from the system menu return to it with the hud") {
  const OptionScript option = parse_option(shipped_document("ui/option.json"));
  const MenuEntry from_system = menu_entry(option, "system");
  CHECK(from_system.exit_to == "system");
  CHECK(from_system.hud);
  const MenuEntry from_menu = menu_entry(option, "main_menu");
  CHECK(from_menu.exit_to == "main_menu");
  CHECK(!from_menu.hud);
  const LoadGameScript load = parse_load_game(shipped_document("ui/load_game.json"));
  CHECK(menu_entry(load, "system").exit_to == "system");
}
