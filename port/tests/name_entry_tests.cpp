// NAME ENTRY: typing (from the pad and the keyboard), deleting, pages, the
// End tab, the no-name notice, the animation, and the quest save it writes.

#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/name_entry.hpp"

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;
const std::string space = "\xe3\x80\x80";

json::JsonValue shipped_document(const char *relative) {
  return DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document(relative);
}

NameEntry make_entry(const std::string &name = "") {
  return NameEntry(parse_name_entry(shipped_document("ui/name_entry.json")),
                   parse_message_box(shipped_document("ui/message_box.json")), QuestSave{name});
}

void run(NameEntry &entry, const double seconds) {
  for (int count = 0; count < static_cast<int>(seconds * 60.0 + 0.5); ++count) {
    entry.update(tick, InputFrame{0, 0, 1});
  }
}

// One tap: down for a tick, then up for one.
void press(NameEntry &entry, const Buttons buttons) {
  entry.update(tick, InputFrame{buttons, buttons, 1});
  entry.update(tick, InputFrame{0, 0, 1});
}

// Past the fade in, the entry takes input.
NameEntry ready_entry(const std::string &name = "") {
  NameEntry entry = make_entry(name);
  run(entry, 0.6);
  static_cast<void>(entry.take_sounds());
  return entry;
}

bool heard(const std::vector<std::string> &sounds, const std::string &name) {
  return std::ranges::find(sounds, name) != sounds.end();
}

// The keyboard's text for one tick, then a quiet tick.
void type_text(NameEntry &entry, const std::string &typed, const int erased = 0,
               const bool submit = false) {
  InputFrame input{0, 0, 1};
  input.typed = typed;
  input.erased = erased;
  input.text_submit = submit;
  entry.update(tick, input);
  entry.update(tick, InputFrame{0, 0, 1});
}

} // namespace

TEST_CASE("the shipped name entry has two pages of 15 x 6 characters and End") {
  const auto script = parse_name_entry(shipped_document("ui/name_entry.json"));
  CHECK(script.pages.size() == 3);
  CHECK(script.columns == 15);
  CHECK(script.rows == 6);
  CHECK(script.pages[0].rows[0][0] == "\xef\xbc\xa1"); // U+FF21 A
  CHECK(script.pages[2].rows.empty());
  CHECK(script.max_length == 8);
  CHECK(script.no_name.size() == 5);
}

TEST_CASE("split_characters keeps multi-byte characters whole") {
  const auto characters = split_characters("A\xef\xbc\xa1\xe3\x80\x80");
  CHECK(characters.size() == 3);
  CHECK(characters[1] == "\xef\xbc\xa1");
  CHECK(characters[2] == space);
}

TEST_CASE("confirm types the character under the cursor; X deletes and Y adds a space") {
  NameEntry entry = ready_entry();
  press(entry, button::a);
  CHECK(entry.name().size() == 1);
  CHECK(entry.name()[0] == "\xef\xbc\xa1");
  press(entry, button::right);
  CHECK(entry.column() == 1);
  CHECK(heard(entry.take_sounds(), "adjust"));
  press(entry, button::a);
  CHECK(entry.name()[1] == "\xef\xbc\xa2"); // B
  press(entry, button::x);
  CHECK(entry.name().size() == 1);
  CHECK(heard(entry.take_sounds(), "delete"));
  press(entry, button::y);
  CHECK(entry.name().size() == 2);
  CHECK(entry.name()[1] == space);
}

TEST_CASE("typing past the last place replaces the last character") {
  NameEntry entry = ready_entry();
  for (int count = 0; count < 10; ++count) {
    press(entry, button::a);
  }
  CHECK(entry.name().size() == 8);
  press(entry, button::right);
  press(entry, button::a);
  CHECK(entry.name().size() == 8);
  CHECK(entry.name()[7] == "\xef\xbc\xa2");
}

TEST_CASE("the columns wrap on a new press and the rows lead to the tabs") {
  NameEntry entry = ready_entry();
  press(entry, button::left);
  CHECK(entry.column() == 14);
  press(entry, button::up);
  CHECK(entry.row() == 0);
  for (int count = 0; count < 6; ++count) {
    press(entry, button::down);
  }
  CHECK(entry.on_tabs());
  press(entry, button::right);
  CHECK(entry.page() == 1);
  CHECK(heard(entry.take_sounds(), "page"));
  press(entry, button::up);
  CHECK(!entry.on_tabs());
  CHECK(entry.row() == 5);
  CHECK(entry.page() == 1);
}

TEST_CASE("the page buttons step through the pages and wrap on a new press") {
  NameEntry entry = ready_entry();
  press(entry, button::right_trigger);
  CHECK(entry.page() == 1);
  CHECK(!entry.on_tabs());
  press(entry, button::right_trigger);
  CHECK(entry.page() == 2);
  CHECK(entry.on_tabs());
  press(entry, button::right_trigger);
  CHECK(entry.page() == 0);
  CHECK(!entry.on_tabs());
  press(entry, button::left_trigger);
  CHECK(entry.page() == 2);
  CHECK(entry.on_tabs());
}

TEST_CASE("cancel jumps to End, and up goes back to the page and row before") {
  NameEntry entry = ready_entry();
  press(entry, button::down);
  press(entry, button::down);
  press(entry, button::b);
  CHECK(entry.page() == 2);
  CHECK(entry.on_tabs());
  press(entry, button::up);
  CHECK(entry.page() == 0);
  CHECK(!entry.on_tabs());
  CHECK(entry.row() == 2);
}

TEST_CASE("ending without a name shows the notice and empties the name") {
  NameEntry entry = ready_entry();
  press(entry, button::y);
  press(entry, button::b);
  press(entry, button::a);
  CHECK(entry.box().is_open());
  CHECK(entry.name().empty());
  CHECK(heard(entry.take_sounds(), "window"));
  run(entry, 0.5);
  press(entry, button::a);
  CHECK(!entry.box().is_open());
  CHECK(!entry.done());
}

TEST_CASE("ending with a name fades out to Club Wired with the name saved") {
  NameEntry entry = ready_entry("old");
  CHECK(entry.name().size() == 3);
  press(entry, button::x);
  press(entry, button::x);
  press(entry, button::x);
  press(entry, button::a);
  press(entry, button::start);
  press(entry, button::b);
  press(entry, button::a);
  CHECK(!entry.box().is_open());
  run(entry, 0.6);
  CHECK(entry.done());
  CHECK(entry.next() == "club_wired");
  CHECK(entry.save().player_name == "\xef\xbc\xa1\xef\xbc\xa1");
}

TEST_CASE("typed keys find their full-width characters and move the cursor there") {
  NameEntry entry = make_entry();
  CHECK(!entry.takes_text());
  run(entry, 0.6);
  CHECK(entry.takes_text());
  type_text(entry, "Ab1");
  const std::vector<std::string> typed{"\xef\xbc\xa1", "\xef\xbd\x82", "\xef\xbc\x91"};
  CHECK(entry.name() == typed);
  CHECK(entry.page() == 0);
  CHECK(entry.row() == 0);
  CHECK(entry.column() == 10);
  CHECK(heard(entry.take_sounds(), "confirm"));
  // ? is on Symbols, - types the long-vowel bar its alias names, a space
  // types the game's space, and keys the pages lack type nothing.
  type_text(entry, "?- #");
  CHECK(entry.page() == 1);
  CHECK(entry.previous_page() == 0);
  REQUIRE(entry.name().size() == 6);
  CHECK(entry.name()[3] == "\xef\xbc\x9f");
  CHECK(entry.name()[4] == "\xe3\x83\xbc");
  CHECK(entry.name()[5] == space);
}

TEST_CASE("Backspace deletes and Enter ends the entry with the typed name") {
  NameEntry entry = ready_entry();
  type_text(entry, "Rex");
  type_text(entry, "", 1);
  CHECK(entry.name().size() == 2);
  CHECK(heard(entry.take_sounds(), "delete"));
  type_text(entry, "", 0, true);
  CHECK(!entry.takes_text());
  run(entry, 0.6);
  CHECK(entry.done());
  CHECK(entry.save().player_name == "\xef\xbc\xb2\xef\xbd\x85");
}

TEST_CASE("Enter without a name opens the notice, which takes no text") {
  NameEntry entry = ready_entry();
  type_text(entry, "  ", 0, true);
  CHECK(entry.box().is_open());
  CHECK(!entry.takes_text());
}

TEST_CASE("the name box grows once the controls box slides in") {
  NameEntry entry = make_entry();
  run(entry, 0.5);
  CHECK(entry.intro() == 0.0F);
  CHECK(entry.name_box_scale()[0] == 0.0F);
  run(entry, 1.0);
  CHECK(entry.intro() == 1.0F);
  CHECK(entry.slide_x() == 289.0F);
  CHECK(entry.hints_alpha() == 1.0F);
  CHECK(entry.name_shown());
  CHECK(entry.name_box_scale()[0] == 1.0F);
  CHECK(entry.page_alpha() == 1.0F);
}

TEST_CASE("the quest save's name, seen events, day and money round-trip through the settings") {
  Settings settings;
  settings.quest.seen_events = {48, 49};
  settings.quest.day = 12;
  settings.quest.money = 4'000'000'000;
  settings.quest.cleared = true;
  settings.quest.riding = "Ixalan";
  settings.quest.spin_paid = 40;
  settings.quest.player_name = "\xef\xbc\xa1\xef\xbd\x82";
  CHECK(settings_from_json(settings_to_json(settings)) == settings);
  CHECK(settings_from_json(json::parse("{}")).quest.player_name.empty());
}
