// BATTLE MUSIC: the programs of owned tracks, the stock they are picked
// from, the play mode, and the music library kept in the save.

#include "test_support.hpp"

#include "phantomcrash/battle_music.hpp"
#include "phantomcrash/data_roots.hpp"

#include <string>
#include <vector>

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

BattleMusicScript shipped_script() {
  return parse_battle_music(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/battle_music.json"));
}

std::vector<MusicCredit> credits() {
  std::vector<MusicCredit> out;
  for (int track = 0; track < 81; ++track) {
    const std::string number = std::to_string(track);
    out.push_back({"sound/" + number + ".WAV", "Title " + number, "Artist " + number});
  }
  return out;
}

MusicLibrary library() {
  MusicLibrary music;
  music.owned = {{3, TrackState::fresh}, {77, TrackState::seen}};
  music.programs[0] = {77};
  return music;
}

void settle(BattleMusic &logic, const double seconds = 0.6) {
  for (double time = 0.0; time < seconds; time += tick) {
    logic.update(tick, InputFrame{});
  }
}

// A press, then long enough for the next panel to slide in.
void tap(BattleMusic &logic, const Buttons buttons) {
  logic.update(tick, InputFrame{buttons, buttons, 1});
  settle(logic, 0.35);
}

} // namespace

TEST_CASE("battle_music.json has the menu, eight programs and a 5 x 3 stock") {
  const BattleMusicScript script = shipped_script();
  CHECK(script.items.size() == 4);
  CHECK(script.program_lit.size() == 8);
  CHECK(script.stock.columns == 5);
  CHECK(script.stock.rows == 3);
  CHECK(script.exit_to == "garage");
  CHECK(script.captions.contains("random"));
}

TEST_CASE("the music library round-trips through JSON") {
  MusicLibrary music = library();
  music.names[2] = "RADIO";
  music.program = 2;
  music.random = true;
  CHECK(music_from_json(music_to_json(music)) == music);
}

TEST_CASE("PROGRAM SET sets the program battles play") {
  BattleMusic logic(shipped_script(), library(), credits(), "garage");
  settle(logic);
  tap(logic, button::a);
  CHECK(logic.view() == BattleMusicView::choose);
  tap(logic, button::down);
  tap(logic, button::a);
  CHECK(logic.view() == BattleMusicView::confirm_set);
  tap(logic, button::a);
  CHECK(logic.view() == BattleMusicView::menu);
  CHECK(logic.music().program == 1);
}

TEST_CASE("PROGRAM EDIT adds and removes owned tracks") {
  BattleMusic logic(shipped_script(), library(), credits(), "garage");
  settle(logic);
  tap(logic, button::down);
  tap(logic, button::a);
  tap(logic, button::a);
  CHECK(logic.view() == BattleMusicView::stock);
  CHECK(logic.cursor_track() == 3);
  CHECK(logic.help_text().find("Title 3") != std::string::npos);
  tap(logic, button::a);
  CHECK((logic.music().programs[0] == std::vector<int>{77, 3}));
  tap(logic, button::x);
  CHECK((logic.music().programs[0] == std::vector<int>{77}));
  tap(logic, button::y);
  CHECK(logic.music_track() == "sound/3.WAV");
  tap(logic, button::b);
  CHECK(logic.on_ok());
  tap(logic, button::a);
  CHECK(logic.view() == BattleMusicView::choose);
  CHECK(logic.previewing() == -1);
}

TEST_CASE("PLAY MODE chooses RANDOM and cancel keeps the old mode") {
  BattleMusic logic(shipped_script(), library(), credits(), "garage");
  settle(logic);
  tap(logic, button::down);
  tap(logic, button::down);
  tap(logic, button::a);
  CHECK(logic.view() == BattleMusicView::play_mode);
  tap(logic, button::right);
  tap(logic, button::b);
  CHECK(!logic.music().random);
  tap(logic, button::a);
  tap(logic, button::right);
  tap(logic, button::a);
  CHECK(logic.music().random);
}

TEST_CASE("EXIT marks the new tracks seen and fades out to the garage") {
  BattleMusic logic(shipped_script(), library(), credits(), "garage");
  settle(logic);
  tap(logic, button::b);
  CHECK(logic.on_exit());
  tap(logic, button::a);
  settle(logic);
  CHECK(logic.done());
  CHECK(logic.next() == "garage");
  CHECK(logic.music().owned.at(3) == TrackState::seen);
}

TEST_CASE("PROGRAM NAME leads to NAME ENTRY") {
  BattleMusic logic(shipped_script(), library(), credits(), "garage");
  settle(logic);
  for (int step = 0; step < 3; ++step) {
    tap(logic, button::down);
  }
  tap(logic, button::a);
  settle(logic);
  CHECK(logic.next() == "name_entry");
}
