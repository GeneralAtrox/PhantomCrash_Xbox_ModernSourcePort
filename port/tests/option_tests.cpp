// The OPTION screen's logic, the system font, the ticker and message boxes.

#include "test_support.hpp"

#include <array>
#include <string>
#include <vector>

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/option_menu.hpp"

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

OptionMenu shipped(const GameOptions options = {}, const std::string &from = {}) {
  const auto roots = DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT);
  return OptionMenu(parse_option(roots.document("ui/option.json")),
                    parse_message_box(roots.document("ui/message_box.json")), options, from);
}

void run(OptionMenu &menu, double seconds) {
  for (int count = 0; count < static_cast<int>(seconds * 60.0 + 0.5); ++count) {
    menu.update(tick, InputFrame{0, 0, 1});
  }
}

// Presses once, and returns the sounds that press made.
std::vector<std::string> tap(OptionMenu &menu, Buttons buttons) {
  menu.update(tick, InputFrame{buttons, buttons, 1});
  return menu.take_sounds();
}

std::string item_id(const OptionMenu &menu) {
  return menu.script().items[static_cast<std::size_t>(menu.cursor())].id;
}

// The shipped screen, faded in, with the cursor on `id`.
OptionMenu ready_at(const std::string &id, const GameOptions options = {}) {
  auto menu = shipped(options);
  run(menu, 0.5);
  while (item_id(menu) != id) {
    static_cast<void>(tap(menu, button::down));
  }
  return menu;
}

using Sounds = std::vector<std::string>;

GlyphFont small_font() {
  return parse_glyph_font(json::parse(R"({"image": "font.png", "cell": 18, "columns": 4,
    "characters": "　ＡＢ？あ", "metrics": [[0, 9], [2, 14], [1, 12]]})"));
}

} // namespace

TEST_CASE("text maps ASCII to the font's full-width glyphs") {
  const auto font = small_font();
  CHECK((glyph_run(font, "AB ?x") == std::vector<int>{1, 2, 0, 3, 3}));
  CHECK((glyph_run(font, "あ") == std::vector<int>{4}));
  const auto quoted = parse_glyph_font(json::parse(R"({"image": "font.png", "cell": 18,
    "columns": 4, "characters": "？’”", "metrics": []})"));
  CHECK((glyph_run(quoted, "'\"") == std::vector<int>{1, 2})); // curly for missing full-width
  const auto dashed = parse_glyph_font(json::parse(R"({"image": "font.png", "cell": 18,
    "columns": 4, "characters": "？―－", "metrics": []})"));
  CHECK((glyph_run(dashed, "—–−") == std::vector<int>{1, 2, 2})); // cp932 dashes stand in
  CHECK(run_width(font, glyph_run(font, "AB ?x")) == 14.0F + 12.0F + 9.0F + 18.0F + 18.0F);
  const auto quads = layout_run(font, {1, 4}, 10.0F, 20.0F);
  CHECK(quads.size() == 2U);
  CHECK((quads[0].rect == std::array<float, 4>{20.0F, 0.0F, 34.0F, 18.0F}));
  CHECK((quads[1].rect == std::array<float, 4>{0.0F, 18.0F, 18.0F, 36.0F}));
  CHECK(quads[1].x == 24.0F);
  CHECK(quads[1].width == 18.0F);
}

TEST_CASE("a ticker scrolls left and repeats through its clip") {
  const auto font = small_font();
  TickerStyle style;
  style.position = {0.0F, 72.0F};
  style.clip = {0.0F, 0.0F, 100.0F, 480.0F};
  style.lead = 10.0F;
  style.trail = 5.0F;
  const auto run = glyph_run(font, "A"); // 14 wide: a copy every 29
  auto glyphs = place_ticker(font, run, style, 0.0);
  CHECK(glyphs.size() == 4U);
  CHECK(glyphs[0].x == 10.0F);
  CHECK(glyphs[3].x == 97.0F);
  glyphs = place_ticker(font, run, style, 0.5); // 30 units on: one past a whole copy
  CHECK(glyphs.front().x == 9.0F);
  CHECK((place_ticker(font, {}, style, 1.0).empty()));
}

TEST_CASE("the option screen fades in, then pulses the cursor on VIBRATION") {
  auto menu = shipped();
  CHECK(menu.music() == "garage");
  CHECK(menu.black_alpha() == 1.0F);
  CHECK(tap(menu, button::a).empty());
  run(menu, 0.5);
  CHECK(menu.black_alpha() == 0.0F);
  CHECK(item_id(menu) == "vibration");
  CHECK(menu.cursor_pulses());
  CHECK((tap(menu, button::up) == Sounds{"cursor"}));
  CHECK(item_id(menu) == "exit");
}

TEST_CASE("the vibration box grows, answers on the current setting and sets it") {
  auto menu = ready_at("vibration");
  CHECK((tap(menu, button::a) == (Sounds{"confirm", "window"})));
  CHECK(menu.box().is_open());
  CHECK(menu.box().kind() == MessageBox::Kind::question);
  CHECK(menu.box().choice() == 0);
  CHECK(!menu.cursor_pulses());
  run(menu, 4.0 / 60.0);
  CHECK(!menu.box().grown());
  run(menu, 2.0 / 60.0);
  CHECK(menu.box().grown());
  // B moves to NO once; A answers.
  CHECK((tap(menu, button::b) == Sounds{"cursor"}));
  CHECK(tap(menu, button::b).empty());
  CHECK((tap(menu, button::a) == Sounds{"confirm"}));
  CHECK(!menu.box().is_open());
  CHECK(!menu.options().vibration);
  static_cast<void>(tap(menu, button::a));
  CHECK(menu.box().choice() == 1);
  CHECK((tap(menu, button::left) == Sounds{"cursor"}));
  static_cast<void>(tap(menu, button::a));
  CHECK(menu.options().vibration);
}

// The pointer over the middle of `sprite`.
InputFrame pointing(const Sprite &sprite, const bool moved, const bool clicked) {
  InputFrame input{0, 0, 1};
  input.pointer = std::array<float, 2>{sprite.position[0] + (sprite.rect[2] - sprite.rect[0]) / 2,
                                       sprite.position[1] + (sprite.rect[3] - sprite.rect[1]) / 2};
  input.pointer_moved = moved;
  input.clicked = clicked;
  return input;
}

const Sprite &item_sprite(const OptionMenu &menu, const std::string &id) {
  const auto &items = menu.script().items;
  for (const auto &item : items) {
    if (item.id == id) {
      return item.normal;
    }
  }
  return items.front().normal;
}

TEST_CASE("the mouse moves the cursor by hovering, confirms by clicking and answers the box") {
  auto menu = shipped();
  run(menu, 0.5);
  menu.update(tick, pointing(item_sprite(menu, "exit"), true, false));
  CHECK(item_id(menu) == "exit");
  CHECK((menu.take_sounds() == Sounds{"cursor"}));
  // Resting on another item without moving leaves the cursor.
  menu.update(tick, pointing(item_sprite(menu, "default"), false, false));
  CHECK(item_id(menu) == "exit");
  menu.update(tick, pointing(item_sprite(menu, "vibration"), false, true));
  CHECK(item_id(menu) == "vibration");
  CHECK(menu.box().is_open());
  run(menu, 6.0 / 60.0);
  CHECK(menu.box().grown());
  menu.update(tick, pointing(menu.box().style().no.normal, false, true));
  CHECK(!menu.box().is_open());
  CHECK(!menu.options().vibration);
}

TEST_CASE("DEFAULT asks, restores the defaults and says so") {
  GameOptions changed{false};
  changed.bgm_volume = 3;
  changed.screen_offset = {5, 5};
  changed.controls.type = 1;
  auto menu = ready_at("default", changed);
  static_cast<void>(tap(menu, button::a));
  CHECK(menu.box().choice() == 1);
  static_cast<void>(tap(menu, button::a));
  CHECK(!menu.box().is_open());
  CHECK(!menu.options().vibration);
  static_cast<void>(tap(menu, button::a));
  static_cast<void>(tap(menu, button::right)); // wraps to YES
  CHECK(menu.box().choice() == 0);
  CHECK((tap(menu, button::a) == (Sounds{"confirm", "window"})));
  CHECK(menu.options() == GameOptions{});
  CHECK(menu.box().kind() == MessageBox::Kind::notice);
  CHECK(tap(menu, button::b).empty());
  CHECK(menu.box().is_open());
  static_cast<void>(tap(menu, button::a));
  CHECK(!menu.box().is_open());
  // The last answer is remembered for the next time.
  static_cast<void>(tap(menu, button::a));
  CHECK(menu.box().choice() == 0);
}

TEST_CASE("cancel moves to EXIT, which fades out to the main menu") {
  auto menu = ready_at("sound_config");
  CHECK((tap(menu, button::b) == Sounds{"cursor"}));
  CHECK(item_id(menu) == "exit");
  CHECK(tap(menu, button::b).empty());
  CHECK((tap(menu, button::a) == Sounds{"confirm"}));
  run(menu, 0.4);
  CHECK(!menu.done());
  CHECK(menu.black_alpha() > 0.5F);
  run(menu, 0.1);
  CHECK(menu.done());
  CHECK(menu.next() == "main_menu");
}

TEST_CASE("each item's help line restarts when the cursor moves onto it") {
  auto menu = ready_at("exit");
  run(menu, 2.0);
  CHECK(menu.help_seconds() > 1.99);
  static_cast<void>(tap(menu, button::down)); // wraps to VIBRATION
  CHECK(menu.help_seconds() < 0.02);
  run(menu, 1.0);
  // A cancel jump back to EXIT carries on its line from where it was.
  static_cast<void>(tap(menu, button::b));
  CHECK(menu.help_seconds() > 1.99);
  CHECK(menu.help_seconds() < 2.1);
}

TEST_CASE("option items lead to their screens and the cursor returns to them") {
  auto menu = ready_at("button_config");
  static_cast<void>(tap(menu, button::a));
  run(menu, 0.5);
  CHECK(menu.done());
  CHECK(menu.next() == "button_config");
  const auto back = shipped({}, "button_config");
  CHECK(item_id(back) == "button_config");
  CHECK((item_id(shipped({}, "main_menu")) == "vibration"));
}

TEST_CASE("the music credit line fills the format") {
  const auto credits = parse_music_credits(json::parse(
      R"({"credits": [{"file": "sound/5GARAGE.WAV", "title": "Sanctuary", "artist": "Dunkirk"}]})"));
  CHECK(credit_line(credits, "sound/5garage.wav", "<{title} : {artist}>") ==
        "<Sanctuary : Dunkirk>");
  CHECK((credit_line(credits, "sound/other.wav", "{title}").empty()));
}

TEST_CASE("option parsing names the entry at fault") {
  const auto roots = DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT);
  auto document = roots.document("ui/option.json");
  json::merge(document, json::parse(R"({"cancel_cursor": "missing"})"));
  CHECK_THROWS(parse_option(document));
  const auto item = [](const char *extra) {
    return json::parse(std::string(R"({"exit_to": "main_menu", "confirm": {"buttons": "a"},
      "cancel": {"buttons": "b"}, "sounds": {}, "default_cursor": "x", "cancel_cursor": "x",
      "help": {"position": [0, 0], "clip": [0, 0, 1, 1]},
      "now_playing": {"position": [0, 0], "clip": [0, 0, 1, 1], "format": ""},
      "items": [{"id": "x", "normal": {"image": "a.png", "rect": [0, 0, 1, 1]},
                 "selected": {"image": "a.png", "rect": [0, 0, 1, 1]}, )") +
                       extra + "}]}");
  };
  CHECK(parse_option(item(R"("exit": true)")).items.size() == 1U);
  CHECK_THROWS(parse_option(item(R"("exit": true, "goto": "y")")));
  CHECK_THROWS(parse_option(item(R"("toggle": {"option": "turbo", "question": []})")));
  CHECK_THROWS(parse_option(item(R"("help": "no action")")));
  CHECK_THROWS(parse_glyph_font(json::parse(R"({"image": "f.png", "cell": 0})")));
}
