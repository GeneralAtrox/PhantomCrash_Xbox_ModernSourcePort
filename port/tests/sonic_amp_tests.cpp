// SONIC AMP: the chart on the television, the SPOT and catalogue grids,
// buying and previewing tracks.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/sonic_amp.hpp"

#include <string>
#include <vector>

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

SonicAmpScript shipped_script() {
  return parse_sonic_amp(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/sonic_amp.json"));
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
  music.owned = {{77, TrackState::seen}};
  music.chart = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
  return music;
}

void settle(SonicAmp &logic, const double seconds = 0.6) {
  for (double time = 0.0; time < seconds; time += tick) {
    logic.update(tick, InputFrame{});
  }
}

void tap(SonicAmp &logic, const Buttons buttons) {
  logic.update(tick, InputFrame{buttons, buttons, 1});
  settle(logic, 0.1);
}

} // namespace

TEST_CASE("sonic_amp.json has the menu, the grids, the actions and the clerk") {
  const SonicAmpScript script = shipped_script();
  CHECK(script.items.size() == 5);
  CHECK(script.items.back().id == "exit");
  CHECK(script.actions.size() == 3);
  CHECK(script.actions[static_cast<std::size_t>(script.first_action)].id == "preview");
  CHECK(script.price == 200);
  CHECK((script.grid.category_pages == std::vector<int>{2, 2, 3}));
  CHECK(script.chart.segments.size() == 10);
  CHECK(script.dialog.says.size() == 4);
  CHECK(script.exit_to == "town_map");
}

TEST_CASE("the chart counts down from No. 10 a rank at a time and wraps") {
  SonicAmp logic(shipped_script(), library(), 0, credits(), "town_map");
  CHECK(logic.rank() == 9);
  CHECK(logic.chart_track(9) == 19);
  settle(logic, 11.05);
  CHECK(logic.rank() == 8);
  settle(logic, 11.0 * 9);
  CHECK(logic.rank() == 9);
}

TEST_CASE("SPOT sells the chart's tracks once, for the price") {
  SonicAmp logic(shipped_script(), library(), 450, credits(), "town_map");
  settle(logic);
  tap(logic, button::a); // SPOT
  CHECK(logic.view() == SonicAmpView::browse);
  CHECK(logic.spot());
  CHECK(logic.columns() == 5);
  CHECK(logic.rows() == 2);
  tap(logic, button::right);
  CHECK(logic.cursor_track() == 11);
  tap(logic, button::a);
  CHECK(logic.view() == SonicAmpView::actions);
  CHECK(logic.saying() == "price");
  tap(logic, button::left); // PREVIEW to BUY
  tap(logic, button::a);
  CHECK(logic.view() == SonicAmpView::message);
  CHECK(logic.saying() == "thanks");
  CHECK(logic.money() == 250);
  CHECK(logic.music().owned.at(11) == TrackState::fresh);
  settle(logic, 2.1);
  CHECK(logic.view() == SonicAmpView::browse);

  tap(logic, button::a);
  tap(logic, button::left);
  tap(logic, button::a);
  CHECK(logic.saying() == "owned");
  CHECK(logic.money() == 250);
  tap(logic, button::a); // confirm closes the message
  CHECK(logic.view() == SonicAmpView::browse);

  tap(logic, button::down);
  CHECK(logic.cursor_track() == 16);
  tap(logic, button::a);
  tap(logic, button::left);
  tap(logic, button::a);
  CHECK(logic.saying() == "thanks");
  CHECK(logic.money() == 50);
  tap(logic, button::a);
  tap(logic, button::right); // track 17
  tap(logic, button::a);
  tap(logic, button::left);
  tap(logic, button::a);
  CHECK(logic.saying() == "poor");
  CHECK(!logic.music().owns(17));
}

TEST_CASE("a catalogue turns its pages, the last with one row of tracks") {
  SonicAmp logic(shipped_script(), library(), 0, credits(), "town_map");
  settle(logic);
  tap(logic, button::right);
  tap(logic, button::right);
  tap(logic, button::right); // 03
  tap(logic, button::a);
  CHECK(logic.view() == SonicAmpView::browse);
  CHECK(logic.pages() == 3);
  CHECK(logic.cursor_track() == 48);
  tap(logic, button::left); // off the left edge: the last page
  CHECK(logic.page() == 2);
  CHECK(logic.rows() == 1);
  CHECK(logic.column() == 5);
  CHECK(logic.cursor_track() == 77);
  tap(logic, button::right_trigger);
  CHECK(logic.page() == 0);
  CHECK(logic.rows() == 2);
  tap(logic, button::b);
  CHECK(logic.view() == SonicAmpView::menu);
}

TEST_CASE("PREVIEW plays the track until cancelled; EXIT leaves for the town map") {
  SonicAmp logic(shipped_script(), library(), 0, credits(), "town_map");
  settle(logic);
  CHECK(logic.music_track() == "dealer");
  tap(logic, button::right); // 01
  tap(logic, button::a);
  tap(logic, button::a);
  tap(logic, button::a); // PREVIEW
  CHECK(logic.view() == SonicAmpView::preview);
  CHECK(logic.music_track() == "sound/0.WAV");
  tap(logic, button::b);
  CHECK(logic.view() == SonicAmpView::browse);
  CHECK(logic.music_track() == "dealer");
  tap(logic, button::b);
  tap(logic, button::b); // cancel moves to EXIT
  CHECK(logic.item().id == "exit");
  tap(logic, button::a);
  settle(logic);
  CHECK(logic.done());
  CHECK(logic.next() == "town_map");
}
