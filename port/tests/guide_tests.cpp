// Pepper's guide: the question list, its topics, answers and memory.

#include "test_support.hpp"

#include <string>
#include <vector>

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/pepper_guide.hpp"

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

PepperGuideScript shipped_guide() {
  return parse_pepper_guide(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/pepper_guide.json"));
}

void run(PepperGuide &guide, const double seconds) {
  for (int count = 0; count < static_cast<int>(seconds * 60.0 + 0.5); ++count) {
    guide.update(tick, InputFrame{0, 0, 1});
  }
}

void press(PepperGuide &guide, const Buttons buttons) {
  guide.update(tick, InputFrame{buttons, buttons, 1});
  guide.update(tick, InputFrame{0, 0, 1});
}

const std::string &selected_text(const PepperGuide &guide) {
  return guide.questions()[static_cast<std::size_t>(guide.selected())].text;
}

} // namespace

TEST_CASE("Pepper's list: nine questions at the top, End last, Pepper in her guide pose") {
  const PepperGuideScript script = shipped_guide();
  CHECK(script.questions.size() == 9U);
  CHECK(script.questions.back().end);
  CHECK(script.questions[1].questions.size() == 5U);
  CHECK(script.questions[0].next == "training_free");
  CHECK(script.guide.character == 12);
  CHECK(script.guide.pose == 2);
  CHECK(script.rows == 5);
}

TEST_CASE("Pepper's list scrolls round under the cursor, five rows at a time") {
  GuideMemory memory;
  PepperGuide guide(shipped_guide(), memory, "club_wired");
  run(guide, 0.6);
  CHECK((guide.rows() == std::vector<int>{0, 1, 2, 3, 4}));
  for (int count = 0; count < 4; ++count) {
    press(guide, button::down);
  }
  CHECK(guide.cursor_row() == 4);
  press(guide, button::down);
  CHECK(guide.cursor_row() == 4);
  CHECK((guide.rows() == std::vector<int>{1, 2, 3, 4, 5}));
  CHECK(guide.selected() == 5);
  // Up past the top row scrolls back, and on round from the first to End.
  for (int count = 0; count < 6; ++count) {
    press(guide, button::up);
  }
  CHECK(guide.cursor_row() == 0);
  CHECK((guide.rows() == std::vector<int>{8, 0, 1, 2, 3}));
  CHECK(selected_text(guide) == "End");
  CHECK(guide.take_sounds().size() == 11U);
}

TEST_CASE("a topic opens its questions; an answer leaves for its talk scene and comes back") {
  GuideMemory memory;
  PepperGuide guide(shipped_guide(), memory, "event_49");
  run(guide, 0.6);
  press(guide, button::down);
  press(guide, button::a);
  CHECK(guide.in_topic());
  CHECK(guide.questions().size() == 5U);
  CHECK((guide.take_sounds() == std::vector<std::string>{"cursor", "confirm"}));
  // Five questions wrap the cursor instead of scrolling.
  press(guide, button::up);
  CHECK(selected_text(guide) == "Provision Items");
  press(guide, button::down);
  press(guide, button::down);
  CHECK(selected_text(guide) == "Where does it take place?");
  press(guide, button::a);
  run(guide, 0.6);
  CHECK(guide.done());
  CHECK(guide.next() == "event_106");

  // Back from the answer, on the same question, the origin kept.
  PepperGuide back(shipped_guide(), memory, "event_106");
  CHECK(back.in_topic());
  CHECK(selected_text(back) == "Where does it take place?");
  CHECK(memory.origin == "event_49");
  run(back, 0.6);
  static_cast<void>(back.take_sounds());
  press(back, button::b);
  CHECK(!back.in_topic());
  CHECK((back.take_sounds() == std::vector<std::string>{"cancel"}));
  CHECK(selected_text(back) == "About Rumbling");
  // The topic's cursor starts again at its top next time.
  press(back, button::a);
  CHECK(selected_text(back) == "What is Rumbling?");
}

TEST_CASE("cancel at the top goes to End; End leads on by where the guide opened from") {
  GuideMemory memory;
  PepperGuide guide(shipped_guide(), memory, "event_49");
  run(guide, 0.6);
  press(guide, button::b);
  CHECK(selected_text(guide) == "End");
  CHECK(guide.cursor_row() == 4);
  CHECK((guide.take_sounds() == std::vector<std::string>{"cursor"}));
  press(guide, button::b);
  CHECK(guide.take_sounds().empty());
  press(guide, button::a);
  run(guide, 0.6);
  CHECK(guide.next() == "town_map");

  GuideMemory other;
  PepperGuide from_club(shipped_guide(), other, "event_129");
  run(from_club, 0.6);
  press(from_club, button::b);
  press(from_club, button::a);
  run(from_club, 0.6);
  CHECK(from_club.next() == "club_wired");
}

TEST_CASE("once the game is cleared, two answers change") {
  const auto ask = [](const bool cleared, const int topic, const int question) {
    GuideMemory memory;
    PepperGuide guide(shipped_guide(), memory, "club_wired", cleared);
    run(guide, 0.6);
    for (int count = 0; count < topic; ++count) {
      press(guide, button::down);
    }
    press(guide, button::a);
    for (int count = 0; count < question; ++count) {
      press(guide, button::down);
    }
    press(guide, button::a);
    run(guide, 0.6);
    return guide.next();
  };
  // About Wire Heads: What is a Ranker?; About Cyber Idols: Who is Mona Lisa?
  CHECK(ask(false, 2, 2) == "event_112");
  CHECK(ask(true, 2, 2) == "event_157");
  CHECK(ask(false, 7, 0) == "event_127");
  CHECK(ask(true, 7, 0) == "event_128");
  CHECK(ask(true, 2, 1) == "event_111");
}
