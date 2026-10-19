// LOAD GAME: the slots, EXIT, the notices, and the save's date and money.

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/load_game.hpp"

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

DataRoots roots() {
  return DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT);
}

LoadGame shipped(std::vector<SaveSlot> slots) {
  return LoadGame(parse_load_game(roots().document("ui/load_game.json")), std::move(slots),
                  parse_message_box(roots().document("ui/message_box.json")));
}

std::vector<SaveSlot> one_game() {
  QuestSave quest;
  quest.player_name = "Tester";
  quest.saved = "2026-09-26 21:05:33";
  std::vector<SaveSlot> slots(3);
  slots[0].quest = quest;
  return slots;
}

void run(LoadGame &menu, const double seconds) {
  for (int count = 0; count < static_cast<int>(seconds * 60.0 + 0.5); ++count) {
    menu.update(tick, InputFrame{0, 0, 1});
  }
}

void press(LoadGame &menu, const Buttons buttons) {
  menu.update(tick, InputFrame{buttons, buttons, 1});
  menu.update(tick, InputFrame{0, 0, 1});
}

// The mouse at `sprite`'s middle, moved there and optionally clicked.
void point(LoadGame &menu, const Sprite &sprite, const bool click) {
  const auto size = sprite.size.value_or(
      std::array{sprite.rect[2] - sprite.rect[0], sprite.rect[3] - sprite.rect[1]});
  InputFrame input{0, 0, 1};
  input.pointer = std::array{sprite.position[0] + (sprite.centered ? 0 : size[0] / 2),
                             sprite.position[1] + (sprite.centered ? 0 : size[1] / 2)};
  input.pointer_moved = true;
  input.clicked = click;
  menu.update(tick, input);
}

} // namespace

TEST_CASE("the mouse points at the slots and EXIT and clicks them and the notices") {
  LoadGame menu = shipped(one_game());
  run(menu, 0.6);
  static_cast<void>(menu.take_sounds());
  const LoadGameScript &script = menu.script();
  point(menu, script.slots[2].plate, false);
  CHECK(menu.slot() == 2);
  CHECK(menu.phase() == LoadGame::Phase::slots);
  point(menu, script.exit.normal, false);
  CHECK(menu.phase() == LoadGame::Phase::exit);
  point(menu, script.slots[0].plate, false);
  CHECK(menu.phase() == LoadGame::Phase::slots);
  CHECK(menu.slot() == 0);
  CHECK((menu.take_sounds() == std::vector<std::string>{"adjust", "cursor", "cursor"}));
  point(menu, script.slots[0].plate, true);
  CHECK(menu.phase() == LoadGame::Phase::loaded);
  run(menu, 0.6);
  point(menu, menu.box().style().ok.normal, true);
  run(menu, 0.6);
  CHECK(menu.next() == "garage");
}

TEST_CASE("with no save LOAD GAME shows only its notice, and confirm leaves") {
  LoadGame menu = shipped(std::vector<SaveSlot>(3));
  CHECK(menu.phase() == LoadGame::Phase::no_records);
  CHECK(menu.box().is_open());
  CHECK(menu.box().lines().size() == 3U);
  run(menu, 0.6);
  press(menu, button::down);
  CHECK(menu.phase() == LoadGame::Phase::no_records);
  press(menu, button::a);
  run(menu, 0.6);
  CHECK(menu.done());
  CHECK(menu.next() == "main_menu");
}

TEST_CASE("the slots wrap sideways; up and down go between them and EXIT") {
  LoadGame menu = shipped(one_game());
  CHECK(menu.phase() == LoadGame::Phase::slots);
  CHECK(menu.slot() == 0);
  static_cast<void>(menu.take_sounds());
  run(menu, 0.6);
  press(menu, button::left);
  CHECK(menu.slot() == 2);
  press(menu, button::right);
  press(menu, button::right);
  CHECK(menu.slot() == 1);
  CHECK((menu.take_sounds() == std::vector<std::string>{"adjust", "adjust", "adjust"}));
  // An empty slot takes the confirm sound and nothing else.
  press(menu, button::a);
  CHECK(menu.phase() == LoadGame::Phase::slots);
  CHECK((menu.take_sounds() == std::vector<std::string>{"confirm"}));
  press(menu, button::down);
  CHECK(menu.phase() == LoadGame::Phase::exit);
  press(menu, button::left);
  CHECK(menu.slot() == 1);
  press(menu, button::up);
  CHECK(menu.phase() == LoadGame::Phase::slots);
  press(menu, button::b);
  CHECK(menu.phase() == LoadGame::Phase::exit);
  CHECK((menu.take_sounds() == std::vector<std::string>{"cursor", "cursor", "cursor"}));
  press(menu, button::b);
  CHECK(menu.take_sounds().empty());
  press(menu, button::a);
  run(menu, 0.6);
  CHECK(menu.next() == "main_menu");
}

TEST_CASE("choosing the save shows the loaded notice, then leads on to the garage") {
  LoadGame menu = shipped(one_game());
  run(menu, 0.6);
  press(menu, button::a);
  CHECK(menu.phase() == LoadGame::Phase::loaded);
  CHECK(menu.box().lines().front() == " Finished loading the play data.");
  press(menu, button::a);
  run(menu, 0.6);
  CHECK(menu.next() == "garage");
}

TEST_CASE("the cursor starts on the latest save") {
  std::vector<SaveSlot> slots = one_game();
  QuestSave later = *slots[0].quest;
  later.saved = "2026-09-27 08:00:00";
  slots[2].quest = later;
  CHECK(shipped(slots).slot() == 2);
}

TEST_CASE("a save's date and time, and the money with NY after it") {
  const LoadGameScript script = parse_load_game(roots().document("ui/load_game.json"));
  CHECK(!parse_saved_time("yesterday"));
  const auto time = parse_saved_time("2026-09-26 21:05:33");
  CHECK(time.has_value());
  if (!time) {
    return;
  }
  CHECK((*time == std::array<int, 6>{2026, 9, 26, 21, 5, 33}));
  const auto sprites = saved_time_sprites(script, script.slots[1], *time);
  // Ten glyphs of the date and eight of the time, 8 apart.
  CHECK(sprites.size() == 18U);
  if (sprites.size() != 18U) {
    return;
  }
  CHECK((sprites[0].position == std::array<float, 2>{279.0F, 215.0F}));
  CHECK(sprites[4].rect == script.saved_time.font.comma.rect);
  CHECK((sprites[10].position == std::array<float, 2>{287.0F, 230.0F}));
  CHECK(sprites[12].rect == script.saved_time.separator.rect);
  CHECK(sprites[17].position[0] == 287.0F + 7 * 8.0F);

  const NumberFont font = parse_frame_style(roots().document("ui/frame.json")).hud.font;
  const auto money = details_money_sprites(font, script.details, 123456);
  // Stored in tens of NY, 1,234,560: seven digits and two commas, then NY.
  CHECK(money.size() == 10U);
  if (money.empty()) {
    return;
  }
  float left = money[0].position[0];
  for (const auto &sprite : money) {
    left = std::min(left, sprite.position[0]);
  }
  CHECK(left == 224.0F);
  CHECK(money.back().position[0] == 224.0F + 7 * 11.0F + 2 * 5.0F + 1.0F);
}

TEST_CASE("SAVE writes an empty slot at once and asks before writing over one") {
  std::vector<int> written;
  SaveLoadHooks hooks;
  hooks.save = [&](const int slot) -> std::optional<QuestSave> {
    written.push_back(slot);
    QuestSave quest;
    quest.player_name = "Tester";
    quest.saved = "2026-09-28 10:00:00";
    return quest;
  };
  LoadGame menu(parse_load_game(roots().document("ui/save_game.json")), one_game(),
                parse_message_box(roots().document("ui/message_box.json")), hooks);
  CHECK(menu.script().saving);
  CHECK(menu.phase() == LoadGame::Phase::slots);
  CHECK(menu.slot() == 0);
  run(menu, 0.6);
  press(menu, button::a);
  REQUIRE(menu.phase() == LoadGame::Phase::question);
  CHECK(menu.box().choice() == 1);
  run(menu, 0.5);
  press(menu, button::a);
  CHECK(menu.phase() == LoadGame::Phase::slots);
  CHECK(written.empty());
  press(menu, button::right);
  press(menu, button::a);
  CHECK(menu.phase() == LoadGame::Phase::saved);
  REQUIRE(written.size() == 1U);
  CHECK(written[0] == 1);
  CHECK(menu.slots()[1].quest.has_value());
  run(menu, 0.5);
  press(menu, button::a);
  CHECK(menu.phase() == LoadGame::Phase::slots);
  press(menu, button::left);
  press(menu, button::a);
  run(menu, 0.5);
  press(menu, button::left);
  CHECK(menu.box().choice() == 0);
  press(menu, button::a);
  CHECK(written.size() == 2U);
  CHECK(menu.phase() == LoadGame::Phase::saved);
}

TEST_CASE("SAVE's overwrite question answers a click on YES or NO") {
  int written = 0;
  SaveLoadHooks hooks;
  hooks.save = [&](int) -> std::optional<QuestSave> {
    ++written;
    return QuestSave{};
  };
  LoadGame menu(parse_load_game(roots().document("ui/save_game.json")), one_game(),
                parse_message_box(roots().document("ui/message_box.json")), hooks);
  run(menu, 0.6);
  const MessageBoxStyle &style = menu.box().style();
  point(menu, menu.script().slots[0].plate, true);
  REQUIRE(menu.phase() == LoadGame::Phase::question);
  run(menu, 0.5);
  point(menu, style.no.normal, true);
  CHECK(menu.phase() == LoadGame::Phase::slots);
  CHECK(written == 0);
  point(menu, menu.script().slots[0].plate, true);
  run(menu, 0.5);
  point(menu, style.yes.normal, true);
  CHECK(menu.phase() == LoadGame::Phase::saved);
  CHECK(written == 1);
}

TEST_CASE("SAVE tells of a failed write; EXIT goes back to the SYSTEM menu") {
  LoadGame menu(parse_load_game(roots().document("ui/save_game.json")), std::vector<SaveSlot>(3),
                parse_message_box(roots().document("ui/message_box.json")));
  run(menu, 0.6);
  press(menu, button::a);
  CHECK(menu.phase() == LoadGame::Phase::saved);
  CHECK(menu.box().lines() == menu.script().failed);
  run(menu, 0.5);
  press(menu, button::a);
  press(menu, button::b);
  CHECK(menu.phase() == LoadGame::Phase::exit);
  press(menu, button::a);
  run(menu, 0.6);
  CHECK(menu.next() == "system");
}

TEST_CASE("with saves it cannot read, SAVE and LOAD only say so, and confirm leaves") {
  for (const char *name : {"ui/save_game.json", "ui/load_game.json"}) {
    LoadGame menu(parse_load_game(roots().document(name)), one_game(),
                  parse_message_box(roots().document("ui/message_box.json")), {}, true);
    CHECK(menu.phase() == LoadGame::Phase::no_records);
    CHECK(menu.box().lines() == menu.script().unreadable);
    CHECK(menu.box().lines().size() == 3U);
    run(menu, 0.6);
    press(menu, button::a);
    run(menu, 0.6);
    CHECK(menu.done());
    CHECK(menu.next() == menu.script().exit_to);
  }
}

TEST_CASE("LOAD GAME takes the chosen slot's game") {
  int loaded = -1;
  SaveLoadHooks hooks;
  hooks.load = [&](const int slot) { loaded = slot; };
  LoadGame menu(parse_load_game(roots().document("ui/load_game.json")), one_game(),
                parse_message_box(roots().document("ui/message_box.json")), hooks);
  run(menu, 0.6);
  press(menu, button::a);
  CHECK(loaded == 0);
  CHECK(menu.phase() == LoadGame::Phase::loaded);
}

TEST_CASE("a screen's frame can give the HUD's SAVE button its own offset from the day") {
  const json::JsonValue document = roots().document("ui/garage.json");
  const json::JsonValue *frame = document.find("frame");
  REQUIRE(frame != nullptr);
  CHECK(parse_frame_placement(*frame).save_offset.has_value());
}

TEST_CASE(
    "the HUD's SAVE button sits between the day and the widest money, and moves with the day") {
  const HudStyle hud = parse_frame_style(roots().document("ui/frame.json")).hud;
  const auto box = hud_save_box(hud);
  REQUIRE(box.has_value());
  CHECK((*box)[0] > hud.day.right[0]);
  float money_left = hud.money.right[0];
  for (const auto &sprite : number_sprites(hud.font, hud.money, 999'999'999, hud.layer)) {
    money_left = std::min(money_left, sprite.position[0]);
  }
  CHECK((*box)[2] < money_left);
  CHECK(!hud.save_button->normal.empty());
  CHECK(hud.save_button->lit.size() == hud.save_button->normal.size());

  const HudStyle moved = placed_hud(hud, {{100, 20}, {300, 20}});
  const auto moved_box = hud_save_box(moved);
  REQUIRE(moved_box.has_value());
  CHECK((*moved_box)[0] - 100 == (*box)[0] - hud.day_label.position[0]);
  CHECK((*moved_box)[1] - 20 == (*box)[1] - hud.day_label.position[1]);
}
