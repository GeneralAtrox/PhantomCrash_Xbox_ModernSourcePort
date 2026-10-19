// The town frame (day, money, music meter), the radio and the location menus
// (Club Wired, the garage).

#include "test_support.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "phantomcrash/location_menu.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/jukebox.hpp"
#include "phantomcrash/location_frame.hpp"

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

json::JsonValue shipped_document(const char *relative) {
  return DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document(relative);
}

FrameStyle shipped_frame() {
  return parse_frame_style(shipped_document("ui/frame.json"));
}

// The digit a number sprite shows (-1 for the comma).
int digit_of(const NumberFont &font, const Sprite &sprite) {
  for (std::size_t digit = 0; digit < font.digits.size(); ++digit) {
    if (font.digits[digit].rect == sprite.rect) {
      return static_cast<int>(digit);
    }
  }
  return -1;
}

LocationMenu make_menu(const std::string &from = "") {
  return LocationMenu(
      parse_location_menu(shipped_document("ui/club_wired.json"), "club_wired.json"), from);
}

LocationMenu make_garage(const std::string &from = "") {
  return LocationMenu(parse_location_menu(shipped_document("ui/garage.json"), "garage.json"), from);
}

void run(LocationMenu &menu, const double seconds) {
  for (int count = 0; count < static_cast<int>(seconds * 60.0 + 0.5); ++count) {
    menu.update(tick, InputFrame{0, 0, 1});
  }
}

void press(LocationMenu &menu, const Buttons buttons) {
  menu.update(tick, InputFrame{buttons, buttons, 1});
  menu.update(tick, InputFrame{0, 0, 1});
}

} // namespace

TEST_CASE("money is written right to left from its edge, a comma before every third digit") {
  const FrameStyle frame = shipped_frame();
  const auto sprites = number_sprites(frame.hud.font, frame.hud.money, 200'000, 0x108);
  CHECK(sprites.size() == 7U);
  if (sprites.size() != 7U) {
    return;
  }
  std::vector<int> shown;
  std::vector<float> x;
  for (const auto &sprite : sprites) {
    shown.push_back(digit_of(frame.hud.font, sprite));
    x.push_back(sprite.position[0]);
    CHECK(sprite.position[1] == 49.0F);
    CHECK(sprite.colour == 0xf0f0f0U);
    CHECK(sprite.layer == 0x108);
  }
  CHECK((shown == std::vector<int>{0, 0, 0, -1, 0, 0, 2}));
  CHECK((x == std::vector<float>{242, 231, 220, 215, 204, 193, 182}));
  CHECK(number_sprites(frame.hud.font, frame.hud.money, 0, 0).size() == 1U);
}

TEST_CASE("the day shows from 01, zero-padded, and too big a number shows nines") {
  const FrameStyle frame = shipped_frame();
  QuestSave quest;
  const auto hud = hud_sprites(frame.hud, quest);
  // DAY-, two digits, one money digit, NY.
  CHECK(hud.size() == 5U);
  CHECK(digit_of(frame.hud.font, hud[1]) == 1);
  CHECK(hud[1].position[0] == 97.0F);
  CHECK(digit_of(frame.hud.font, hud[2]) == 0);
  const auto nines = number_sprites(frame.hud.font, frame.hud.day, 123, 0);
  CHECK(nines.size() == 2U);
  CHECK(std::ranges::all_of(
      nines, [&](const Sprite &sprite) { return digit_of(frame.hud.font, sprite) == 9; }));
}

TEST_CASE("a shop's price ends in NY one past its digits, as the money writer puts it") {
  const FrameStyle frame = shipped_frame();
  NumberStyle price = frame.hud.money;
  price.right = {307.0F, 249.0F}; // USED-SV's (0x02344a)
  const auto sprites = money_sprites(frame.hud, price, 79000, 0x102);
  REQUIRE(sprites.size() == 7U); // 79,000 and NY
  const Sprite &unit = sprites.back();
  CHECK(unit.rect == frame.hud.money_unit.rect);
  CHECK(unit.position[0] == 308.0F);
  CHECK(unit.position[1] == 249.0F);
  CHECK(unit.layer == 0x102);
}

TEST_CASE("the sort hints light the key sorted by, its arrow turning with the order") {
  const SortHintStyle &style = shipped_frame().sort_hints;
  const auto by_name = sort_hint_sprites(style, 0, true);
  REQUIRE(by_name.size() == 6U); // a button, a label and an arrow for each key
  CHECK(by_name[0].colour == style.lit);
  CHECK(by_name[3].colour == style.dim);
  CHECK(by_name[2].rect == style.up.rect);
  CHECK(by_name[2].position == style.keys[0].arrow_at);
  const auto turned = sort_hint_sprites(style, 1, false);
  CHECK(turned[0].colour == style.dim);
  CHECK(turned[5].colour == style.lit);
  CHECK(turned[5].rect == style.down.rect);
  CHECK(turned[2].rect == style.up.rect); // the other key's keeps pointing up
}

TEST_CASE("the music meter rises to what is heard at once and dies away in silence") {
  const FrameStyle frame = shipped_frame();
  CHECK(frame.meter.music.window == 2048);
  CHECK(frame.meter.music.centre == 0.3F);
  MusicMeter meter(frame.meter);
  CHECK(meter.levels().size() == 7U);
  meter.update(tick, std::vector<float>(7, 0.5F));
  CHECK(std::ranges::all_of(meter.levels(), [](const float level) { return level == 0.5F; }));
  meter.update(tick, std::vector<float>(7, 0.0F));
  CHECK(std::ranges::all_of(meter.levels(), [](const float level) { return level < 0.5F; }));
  for (int count = 0; count < 60; ++count) {
    meter.update(tick, {});
  }
  CHECK(std::ranges::all_of(meter.levels(), [](const float level) { return level == 0.0F; }));
  // Silent, only the dark parts; lit, a bar's lit part sits on its bottom.
  CHECK(meter_sprites(frame.meter, {434, 315}, meter.levels()).size() == 7U);
  const auto lit = meter_sprites(frame.meter, {434, 315}, std::vector<float>(7, 0.5F));
  CHECK(lit.size() == 14U);
  CHECK(lit[1].position[1] == 327.0F);
  CHECK((*lit[1].size)[1] == 12.0F);
  CHECK(lit[2].position[0] == 441.0F);
}

TEST_CASE("the radio deals a playlist on Club Wired and fills silences after five seconds") {
  std::vector<std::string> songs;
  for (int index = 0; index < 81; ++index) {
    songs.push_back("sound/" + std::to_string(index) + ".WAV");
  }
  Jukebox radio(parse_jukebox(shipped_document("audio/radio.json")), songs, 3);
  // No playlist before Club Wired has opened.
  CHECK(!radio.update("name_entry", 10.0, true));
  CHECK(!radio.open("name_entry"));
  const auto first = radio.open("club_wired");
  CHECK(first.has_value());
  const std::set<std::string> dealt(radio.playlist().begin(), radio.playlist().end());
  CHECK(dealt.size() == 16U);
  CHECK(dealt.contains(*first));
  CHECK(!radio.update("club_wired", 1.0, false));
  for (int count = 0; count < 299; ++count) {
    CHECK(!radio.update("club_wired", tick, true));
  }
  const auto next = radio.update("club_wired", tick, true);
  CHECK(next.has_value() && dealt.contains(*next));
  // Talk scenes do not run the radio.
  CHECK(!radio.update("event_48", 10.0, true));
}

TEST_CASE("the Club Wired cursor starts on the item for the screen before") {
  CHECK(make_menu().cursor() == 0);
  CHECK(make_menu("pepper_guide").cursor() == 1);
  CHECK(make_menu("name_entry").cursor() == 4);
  auto from_map = make_menu("town_map");
  CHECK((from_map.take_sounds() == std::vector<std::string>{"club_arrive"}));
}

TEST_CASE("the Club Wired wheel turns to the cursor over eight frames, the short way round") {
  auto menu = make_menu();
  run(menu, 0.6);
  auto rest = menu.wedges();
  CHECK(rest.size() == 8U);
  CHECK(rest[0].first == 5);
  CHECK(rest[0].second == -45.0F);
  CHECK(rest[1].first == 0);
  CHECK(rest[1].second == 0.0F);
  press(menu, button::left);
  CHECK(menu.cursor() == 5);
  CHECK(menu.turning());
  CHECK((menu.take_sounds() == std::vector<std::string>{"wheel"}));
  // A tick into the turn from 0 to 5: an eighth of the way, clockwise.
  CHECK(std::abs(menu.wedges()[1].second - 45.0F / 8.0F) < 0.5F);
  press(menu, button::right); // ignored mid-turn
  run(menu, 0.2);
  CHECK(!menu.turning());
  CHECK(menu.cursor() == 5);
  CHECK(menu.wedges()[1].first == 5);
  CHECK(menu.wedges()[1].second == 0.0F);
}

TEST_CASE("Club Wired: cancel goes to Exit, confirm leaves for the item's screen") {
  auto menu = make_menu("pepper_guide");
  run(menu, 0.6);
  press(menu, button::b);
  CHECK(menu.cursor() == 5);
  run(menu, 0.2);
  static_cast<void>(menu.take_sounds());
  press(menu, button::b);
  CHECK(menu.take_sounds().empty());
  press(menu, button::a);
  CHECK((menu.take_sounds() == std::vector<std::string>{"club_exit"}));
  run(menu, 0.6);
  CHECK(menu.done());
  CHECK(menu.next() == "town_map");

  auto training = make_menu("pepper_guide");
  run(training, 0.6);
  press(training, button::a);
  CHECK((training.take_sounds() == std::vector<std::string>{"confirm"}));
  run(training, 0.6);
  CHECK(training.next() == "pepper_guide");
}

TEST_CASE("the RIDING SV plate slides in from the left over eight frames") {
  const FrameStyle frame = shipped_frame();
  const RidingPlateStyle &plate = frame.riding_plate;
  CHECK(plate.plate.position[1] == 399.0F);
  CHECK(riding_plate_offset(plate, 0.0) == -320.0F);
  CHECK(std::abs(riding_plate_offset(plate, 4.0 / 60.0) + 160.0F) < 1.0F);
  CHECK(riding_plate_offset(plate, 1.0) == 0.0F);
  const auto club = shipped_document("ui/club_wired.json");
  CHECK(parse_frame_placement(*club.find("frame")).riding_plate);
}

TEST_CASE("the garage: eight items on Club Wired's wheel, its own music and dark help") {
  const LocationMenuScript script =
      parse_location_menu(shipped_document("ui/garage.json"), "garage.json");
  const LocationMenuScript club =
      parse_location_menu(shipped_document("ui/club_wired.json"), "club_wired.json");
  CHECK(script.scene == 13);
  CHECK(script.music == "garage");
  CHECK(script.items.size() == 8U);
  CHECK(script.items.back().next == "town_map");
  CHECK(script.help.colour == 0x3c3c3cU);
  CHECK(script.wheel.position == club.wheel.position);
  CHECK(script.wheel.pivot == club.wheel.pivot);
  CHECK(script.frame.meter == club.frame.meter);
  // All eight slots show a different item.
  std::set<int> shown;
  for (const auto &[item, angle] : make_garage().wedges()) {
    shown.insert(item);
  }
  CHECK(shown.size() == 8U);
}

TEST_CASE("the garage: cancel goes to the way out, X to the hangar and Y to sleep") {
  LocationMenu menu = make_garage("town_map");
  CHECK((menu.take_sounds() == std::vector<std::string>{"club_arrive"}));
  run(menu, 0.6);
  press(menu, button::y);
  CHECK(menu.cursor() == 5);
  // The wheel turns the whole way at once: five steps.
  CHECK(menu.turning());
  run(menu, 0.3);
  press(menu, button::b);
  CHECK(menu.cursor() == 7);
  run(menu, 0.3);
  press(menu, button::x);
  CHECK(menu.cursor() == 0);
  CHECK((menu.take_sounds() == std::vector<std::string>{"wheel", "wheel", "wheel"}));
  run(menu, 0.3);
  press(menu, button::x);
  CHECK(menu.take_sounds().empty());
  press(menu, button::a);
  run(menu, 0.6);
  CHECK(menu.next() == "hangar");
}

TEST_CASE("the garage starts on the item for the screen it came back from") {
  CHECK(make_garage("records").cursor() == 3);
  CHECK(make_garage("sleep").cursor() == 0);
  CHECK(make_garage("system").cursor() == 6);
  CHECK(make_garage("town_map").cursor() == 0);
}

TEST_CASE("a shortcut to an unknown item is refused, naming the document") {
  std::ifstream file(std::string(PHANTOMCRASH_SOURCE_ROOT) + "/data/ui/garage.json");
  std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const std::string known = R"("item": "sleep")";
  const auto at = text.find(known);
  CHECK(at != std::string::npos);
  if (at == std::string::npos) {
    return;
  }
  text.replace(at, known.size(), R"("item": "nowhere")");
  std::string message;
  try {
    static_cast<void>(parse_location_menu(json::parse(text), "test.json"));
  } catch (const std::runtime_error &error) {
    message = error.what();
  }
  CHECK(message == "test.json: shortcut to unknown item \"nowhere\"");
}
