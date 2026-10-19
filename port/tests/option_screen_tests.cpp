// SOUND CONFIG, BUTTON CONFIG and SCREEN ADJUST, the volume curve, and the
// game options they keep.

#include "test_support.hpp"

#include <array>
#include <string>
#include <vector>

#include "phantomcrash/button_config.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/option_menu.hpp"
#include "phantomcrash/screen_adjust.hpp"
#include "phantomcrash/sound_config.hpp"

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;
using Sounds = std::vector<std::string>;

json::JsonValue shipped_document(const char *relative) {
  return DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document(relative);
}

template <typename Menu> void run(Menu &menu, double seconds, Buttons held = 0) {
  for (int count = 0; count < static_cast<int>(seconds * 60.0 + 0.5); ++count) {
    menu.update(tick, InputFrame{held, 0, 1});
  }
}

template <typename Menu> Sounds tap(Menu &menu, Buttons buttons) {
  menu.update(tick, InputFrame{buttons, buttons, 1});
  return menu.take_sounds();
}

SoundConfigMenu sound_config(const GameOptions options = {}) {
  SoundConfigMenu menu(parse_sound_config(shipped_document("ui/sound_config.json")), options);
  run(menu, 0.5);
  return menu;
}

ButtonConfigMenu button_config(const GameOptions options = {}) {
  ButtonConfigMenu menu(parse_button_config(shipped_document("ui/button_config.json")), options);
  run(menu, 0.5);
  return menu;
}

ScreenAdjustMenu screen_adjust(const GameOptions options = {}) {
  ScreenAdjustMenu menu(parse_screen_adjust(shipped_document("ui/screen_adjust.json")),
                        parse_message_box(shipped_document("ui/message_box.json")), options);
  run(menu, 0.5);
  return menu;
}

template <typename Menu> std::string finish(Menu &menu) {
  run(menu, 0.5);
  CHECK(menu.done());
  return menu.next();
}

} // namespace

TEST_CASE("game options round-trip and reject bad values") {
  Settings changed;
  changed.game.vibration = false;
  changed.game.bgm_volume = 12;
  changed.game.se_volume = 0;
  changed.game.screen_offset = {-32, 14};
  changed.game.controls = {1, 1, 1, {0, 1, 2, 3, 4, 5, 6, 7}};
  CHECK(settings_from_json(json::parse(json::write(settings_to_json(changed)))) == changed);

  const auto odd = settings_from_json(json::parse(R"({"game": {"bgm_volume": 900,
    "screen_offset": [500, -3], "controls": {"type": 7, "bindings": {"jump": "b"}}}})"));
  CHECK(odd.game.bgm_volume == 255);
  CHECK((odd.game.screen_offset == std::array<int, 2>{64, -3}));
  CHECK(odd.game.controls.type == 1);
  // "b" would be on two actions, so the default assignments stay.
  CHECK(odd.game.controls.bindings == ControlOptions{}.bindings);
}

TEST_CASE("volume levels follow the original's millibel table") {
  std::vector<int> table(256, -100);
  table[0] = -9600;
  table[255] = 0;
  CHECK(volume_gain(0, table) == 0.0F);
  CHECK(volume_gain(255, table) == 1.0F);
  CHECK(volume_gain(100, table) > 0.89F);
  CHECK(volume_gain(100, table) < 0.90F);
  // Without the table, the stand-in curve: 128 is about -685 mB.
  CHECK(volume_gain(0) == 0.0F);
  CHECK(volume_gain(128) > 0.45F);
  CHECK(volume_gain(128) < 0.46F);
  CHECK(volume_gain(300) == 1.0F);
}

TEST_CASE("SE test numbers run through the banks in order") {
  const auto script = parse_sound_config(shipped_document("ui/sound_config.json"));
  CHECK(test_sound_file(script.test_groups, 0) == "sound/sfx/battle_player/00.wav");
  CHECK(test_sound_file(script.test_groups, 51) == "sound/sfx/battle_old/00.wav");
  CHECK(test_sound_file(script.test_groups, 54) == "sound/sfx/battle/00.wav");
  CHECK(test_sound_file(script.test_groups, 149) == "sound/sfx/sys/47.wav");
  CHECK(test_sound_file(script.test_groups, 150).empty());
}

TEST_CASE("sound config changes the volumes as they are chosen") {
  auto menu = sound_config();
  CHECK(menu.cursor() == 0);
  CHECK((tap(menu, button::left) == Sounds{"cursor"}));
  CHECK(menu.options().bgm_volume == 254);
  // A new press wraps around the ends; a held one repeats and stops there.
  CHECK((tap(menu, button::right) == Sounds{"cursor"}));
  CHECK((tap(menu, button::right) == Sounds{"cursor"}));
  CHECK(menu.options().bgm_volume == 0);
  run(menu, 1.0, button::right);
  CHECK(menu.options().bgm_volume > 10);
  static_cast<void>(tap(menu, button::down));
  static_cast<void>(tap(menu, button::left));
  CHECK(menu.options().se_volume == 224);
  CHECK(menu.value(1) == 224);
}

TEST_CASE("sound config plays the test effect and leaves only by EXIT") {
  auto menu = sound_config();
  static_cast<void>(tap(menu, button::down));
  static_cast<void>(tap(menu, button::down));
  static_cast<void>(tap(menu, button::right));
  CHECK(menu.value(2) == 1);
  CHECK((tap(menu, button::a) == Sounds{"sound/sfx/battle_player/01.wav"}));
  CHECK((tap(menu, button::b) == Sounds{"cursor"}));
  CHECK(menu.cursor() == 3);
  CHECK(tap(menu, button::b).empty());
  CHECK((tap(menu, button::a) == Sounds{"confirm"}));
  CHECK(finish(menu) == "option");
}

TEST_CASE("button config assigns a pressed button and swaps the old one") {
  auto menu = button_config();
  for (int row = 0; row < 3; ++row) {
    static_cast<void>(tap(menu, button::down));
  }
  CHECK(menu.script().rows[3].id == "right_arm_weapons");
  CHECK(menu.binding(3) == 5); // right trigger
  // A is JUMP's by default: RIGHT ARM takes it, JUMP gets the right trigger.
  CHECK(tap(menu, button::a).empty());
  CHECK(menu.binding(3) == 0);
  CHECK(menu.options().controls.bindings[6] == 5);
  // B is assignable too, and does not leave.
  static_cast<void>(tap(menu, button::b));
  CHECK(menu.binding(3) == 1);
  CHECK(!menu.done());
}

TEST_CASE("button config choices flip, DEFAULT restores the buttons, EXIT leaves") {
  GameOptions options;
  options.controls.bindings = {0, 1, 2, 3, 4, 5, 6, 7};
  auto menu = button_config(options);
  CHECK((tap(menu, button::right) == Sounds{"cursor"}));
  CHECK(menu.options().controls.type == 1);
  CHECK(menu.choice(0) == 1);
  static_cast<void>(tap(menu, button::up)); // wraps to EXIT
  static_cast<void>(tap(menu, button::up)); // DEFAULT
  CHECK((tap(menu, button::a) == Sounds{"confirm"}));
  CHECK(menu.options().controls.bindings == ControlOptions{}.bindings);
  CHECK(menu.options().controls.type == 1);
  static_cast<void>(tap(menu, button::down));
  CHECK((tap(menu, button::a) == Sounds{"confirm"}));
  CHECK(finish(menu) == "option");
}

TEST_CASE("button config follows the mouse over its bars") {
  auto menu = button_config();
  const auto &script = menu.script();
  // The middle of row `index`'s bar, or `offset` into it.
  const auto on_row = [&](const std::size_t index, const std::array<float, 2> offset = {0, 0}) {
    const ButtonRow &row = script.rows[index];
    const Sprite &bar = row.bar_normal.value_or(script.bar_normal);
    const float half_x = (bar.rect[2] - bar.rect[0]) * 0.5F;
    return std::array{row.bar[0] + (offset[0] > 0 ? offset[0] : half_x),
                      row.bar[1] + (offset[1] > 0 ? offset[1] : 13.0F)};
  };
  const auto mouse = [&](const std::array<float, 2> at, const bool click) {
    InputFrame input{0, 0, 1};
    input.pointer = at;
    input.pointer_moved = true;
    input.clicked = click;
    menu.update(tick, input);
    return menu.take_sounds();
  };
  CHECK((mouse(on_row(2), false) == Sounds{"cursor"}));
  CHECK(menu.cursor() == 2);
  // A click on a choice row steps its value; on the left arrow, back.
  CHECK((mouse(on_row(0), true) == Sounds{"cursor", "cursor"}));
  CHECK(menu.choice(0) == 1);
  const auto &arrow = script.left_arrow.front();
  static_cast<void>(mouse({script.rows[0].left_arrow[0] + (arrow.rect[2] - arrow.rect[0]) * 0.5F,
                           script.rows[0].left_arrow[1] + (arrow.rect[3] - arrow.rect[1]) * 0.5F},
                          true));
  CHECK(menu.choice(0) == 0);
  // A click on EXIT leaves.
  CHECK((mouse(on_row(script.rows.size() - 1), true) == Sounds{"cursor", "confirm"}));
  CHECK(finish(menu) == "option");
}

TEST_CASE("screen adjust moves the picture within its range") {
  auto menu = screen_adjust();
  CHECK((tap(menu, button::right) == Sounds{"adjust"}));
  CHECK((tap(menu, button::down) == Sounds{"cursor"}));
  CHECK((menu.offset() == std::array<int, 2>{1, 1}));
  run(menu, 10.0, button::left | button::up);
  CHECK((menu.offset() == std::array<int, 2>{-32, -16}));
  static_cast<void>(menu.take_sounds());
  CHECK(tap(menu, button::left).empty());
  static_cast<void>(tap(menu, button::x));
  CHECK((menu.offset() == std::array<int, 2>{0, 0}));
}

TEST_CASE("screen adjust keeps the offset on YES and puts it back on NO") {
  GameOptions start;
  start.screen_offset = {3, -2};
  auto menu = screen_adjust(start);
  static_cast<void>(tap(menu, button::right));
  CHECK((tap(menu, button::a) == Sounds{"confirm", "window"}));
  CHECK(menu.box().choice() == 1);
  static_cast<void>(tap(menu, button::a));
  CHECK((menu.offset() == std::array<int, 2>{3, -2}));
  CHECK(finish(menu) == "option");

  auto kept = screen_adjust(start);
  static_cast<void>(tap(kept, button::right));
  static_cast<void>(tap(kept, button::a));
  static_cast<void>(tap(kept, button::left)); // YES
  static_cast<void>(tap(kept, button::a));
  CHECK((kept.offset() == std::array<int, 2>{4, -2}));
}

TEST_CASE("sub-screen parsing names the entry at fault") {
  auto sound = shipped_document("ui/sound_config.json");
  json::merge(sound, json::parse(R"({"cancel_cursor": "missing"})"));
  CHECK_THROWS(parse_sound_config(sound));
  auto adjust = shipped_document("ui/screen_adjust.json");
  json::merge(adjust, json::parse(R"({"range_x": [2, 30]})"));
  CHECK_THROWS(parse_screen_adjust(adjust));
  auto buttons = shipped_document("ui/button_config.json");
  json::merge(buttons, json::parse(R"({"default_cursor": "missing"})"));
  CHECK_THROWS(parse_button_config(buttons));
}

TEST_CASE("OPTION, SOUND CONFIG and BUTTON CONFIG place the CURRENT MUSIC meter") {
  // The top left of the band's first painted slot, so the bars cover the slots.
  const std::array<float, 2> slot{435.0F, 396.0F};
  CHECK(parse_option(shipped_document("ui/option.json")).meter == slot);
  CHECK(parse_sound_config(shipped_document("ui/sound_config.json")).meter == slot);
  CHECK(parse_button_config(shipped_document("ui/button_config.json")).meter == slot);
}
