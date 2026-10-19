// KEEP SV: the kept Scoobees' list, SAVE through the hangar, SPEC, DELETE
// and the VS Scoobees' save format.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/keep_sv.hpp"

#include <string>
#include <vector>

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

KeepSvScript shipped_script() {
  return parse_keep_sv(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/keep_sv.json"));
}

// Makers 0 and 3 for bodies 0 and 1.
PartTables tables() {
  PartTables parts;
  parts.bodies.assign(2, ArmourRecord{});
  parts.bodies[1].maker = 3;
  return parts;
}

VsScoobee scoobee(const std::string &name, const int body = 0) {
  VsScoobee kept;
  kept.name = name;
  kept.robot.name = name;
  kept.robot.parts[static_cast<std::size_t>(PartSlot::body)] = RobotPart{body, 0};
  kept.saved = "2026-09-28 10:20:30";
  return kept;
}

// Hangar 0 holds a complete robot named ALPHA.
QuestSave complete_quest() {
  QuestSave quest;
  quest.garage = new_game_garage(1);
  Hangar &bay = quest.garage.hangars[0];
  bay.name = "ALPHA";
  const auto fit = [&](const PartSlot slot, const PartKind kind) {
    bay.parts[static_cast<std::size_t>(slot)] = quest.garage.add_stock({kind, 0, 1, "CHIPPY", 0});
  };
  fit(PartSlot::body, PartKind::body);
  fit(PartSlot::legs, PartKind::legs);
  fit(PartSlot::chip, PartKind::chip);
  fit(PartSlot::right_arm, PartKind::arm);
  return quest;
}

struct Fixture {
  PartTables parts = tables();
  std::vector<VsScoobee> kept;
  KeepSvCursor cursor;

  KeepSv open(KeepSvEntry entry = {}, QuestSave quest = {}) {
    KeepSv logic(shipped_script(), parts, std::move(quest), kept, cursor, std::move(entry));
    settle(logic);
    return logic;
  }

  static void settle(KeepSv &logic, const double seconds = 1.0) {
    for (double time = 0.0; time < seconds; time += tick) {
      logic.update(tick, InputFrame{});
    }
  }
};

void tap(KeepSv &logic, const Buttons buttons) {
  logic.update(tick, InputFrame{buttons, buttons, 1});
  Fixture::settle(logic, 0.3);
}

bool played(KeepSv &logic, const std::string &sound) {
  for (const std::string &name : logic.take_sounds()) {
    if (name == sound) {
      return true;
    }
  }
  return false;
}

} // namespace

TEST_CASE("keep_sv.json reads the slots, the popups and the messages") {
  const KeepSvScript script = shipped_script();
  CHECK(script.slots.maker_cards.size() == 6);
  CHECK(script.slots.visible == 5);
  CHECK(script.popups.at("choose").size() == 3);
  CHECK(script.popups.at("damaged").size() == 2);
  CHECK(script.popups.at("save").front().option == KeepSvOption::make);
  CHECK(script.messages.contains("overwrite"));
  CHECK(script.spec_route == "sv_spec");
  CHECK(script.make_route == "hangar");
}

TEST_CASE("from the SYSTEM menu the cursor starts on SAVE, which needs a complete Scoobee") {
  Fixture fixture;
  fixture.cursor = {3, 2, 0};
  KeepSv none = fixture.open();
  CHECK(none.view() == KeepSvView::save);
  CHECK(fixture.cursor.first == 0);
  tap(none, none.script().confirm_buttons);
  CHECK(none.view() == KeepSvView::notice);
  CHECK(none.message() == "none");
  tap(none, none.script().confirm_buttons);
  CHECK(none.view() == KeepSvView::save);
  KeepSv make = fixture.open({}, complete_quest());
  tap(make, make.script().confirm_buttons);
  REQUIRE(make.popup() != nullptr);
  CHECK(make.message() == "save");
  tap(make, make.script().confirm_buttons);
  Fixture::settle(make);
  CHECK(make.next() == "hangar");
}

TEST_CASE("the hangar's Scoobee is kept at the end, or takes the place of its namesake") {
  Fixture fixture;
  fixture.kept = {scoobee("ALPHA"), scoobee("BETA")};
  const QuestSave quest = complete_quest();
  VsScoobee alpha = vs_scoobee_from_hangar(quest.garage, 0, "2026-09-28 12:00:00");
  CHECK(alpha.name == "ALPHA");
  CHECK(alpha.chip_name == "CHIPPY");
  CHECK(alpha.robot.part(PartSlot::right_arm).has_value());
  KeepSv overwrite = fixture.open({false, alpha});
  CHECK(overwrite.view() == KeepSvView::question);
  CHECK(overwrite.message() == "overwrite");
  CHECK(overwrite.choice() == 1);
  tap(overwrite, overwrite.script().left);
  tap(overwrite, overwrite.script().confirm_buttons);
  CHECK(overwrite.message() == "saved");
  REQUIRE(fixture.kept.size() == 2);
  CHECK(fixture.kept[0].saved == "2026-09-28 12:00:00");
  alpha.name = "GAMMA";
  KeepSv added = fixture.open({false, alpha});
  CHECK(added.message() == "saved");
  CHECK(fixture.kept.size() == 3);
  CHECK(fixture.kept.back().name == "GAMMA");
}

TEST_CASE("the list scrolls past its fifth row and an empty row buzzes") {
  Fixture fixture;
  for (int index = 0; index < 7; ++index) {
    fixture.kept.push_back(scoobee("SV" + std::to_string(index), index % 2));
  }
  KeepSv logic = fixture.open();
  const KeepSvScript &script = logic.script();
  tap(logic, script.up);
  CHECK(logic.view() == KeepSvView::list);
  CHECK(logic.shows_details());
  CHECK(logic.maker(1) == 3);
  for (int step = 0; step < 4; ++step) {
    tap(logic, script.right);
  }
  CHECK(logic.cursor().row == 4);
  logic.update(tick, InputFrame{script.right, script.right, 1});
  CHECK(logic.scrolling());
  CHECK(logic.cursor().first == 1);
  CHECK(logic.shown_first() == 0);
  Fixture::settle(logic, 0.5);
  CHECK(!logic.scrolling());
  CHECK(logic.chosen() == 5);
  fixture.kept.resize(2);
  fixture.cursor = {0, 4, 0};
  KeepSv few = fixture.open({false, {}});
  static_cast<void>(few.take_sounds());
  tap(few, few.script().confirm_buttons);
  CHECK(few.view() == KeepSvView::list);
  CHECK(played(few, few.script().no_sound));
}

TEST_CASE("SPEC leaves with the cursor kept; DELETE asks first and pages back") {
  Fixture fixture;
  for (int index = 0; index < 6; ++index) {
    fixture.kept.push_back(scoobee("SV" + std::to_string(index)));
  }
  fixture.cursor = {1, 4, 0};
  KeepSv spec = fixture.open({false, {}});
  tap(spec, spec.script().confirm_buttons);
  REQUIRE(spec.popup() != nullptr);
  CHECK(spec.message() == "choose");
  tap(spec, spec.script().confirm_buttons);
  Fixture::settle(spec);
  CHECK(spec.next() == "sv_spec");
  CHECK(fixture.cursor.first + fixture.cursor.row == 5);
  KeepSv remove = fixture.open({false, {}});
  tap(remove, remove.script().confirm_buttons);
  tap(remove, remove.script().right);
  tap(remove, remove.script().confirm_buttons);
  CHECK(remove.message() == "delete");
  CHECK(remove.choice() == 1);
  tap(remove, remove.script().left);
  tap(remove, remove.script().confirm_buttons);
  CHECK(fixture.kept.size() == 5);
  CHECK(fixture.kept.back().name == "SV4");
  CHECK(fixture.cursor.first == 0);
  CHECK(remove.view() == KeepSvView::list);
}

TEST_CASE("a damaged Scoobee offers only DELETE; cancel goes to EXIT, which leaves") {
  Fixture fixture;
  fixture.kept = {scoobee("BROKEN", 9)};
  fixture.cursor = {0, 0, 0};
  KeepSv logic = fixture.open({false, {}});
  CHECK(logic.damaged(0));
  CHECK(!logic.shows_details());
  tap(logic, logic.script().confirm_buttons);
  CHECK(logic.message() == "damaged");
  tap(logic, logic.script().cancel_buttons);
  CHECK(logic.choice() == 1);
  tap(logic, logic.script().confirm_buttons);
  CHECK(logic.view() == KeepSvView::list);
  tap(logic, logic.script().cancel_buttons);
  CHECK(logic.view() == KeepSvView::exit);
  tap(logic, logic.script().confirm_buttons);
  Fixture::settle(logic);
  CHECK(logic.next() == "system");
}

TEST_CASE("the VS Scoobees read back as they were written") {
  std::vector<VsScoobee> kept{scoobee("ALPHA"), scoobee("BETA", 1)};
  kept[1].robot.modules = {3, 7};
  kept[1].robot.paint = 4;
  kept[1].chip_name = "CHIPPY";
  CHECK(vs_scoobees_from_json(vs_scoobees_to_json(kept)) == kept);
  CHECK(vs_scoobees_from_json(json::parse("[1, {\"name\": \"X\"}]")).size() == 1);
}

TEST_CASE("VS Scoobees saved with the older slot keys keep their sides") {
  // Older saves named side 0, the RIGHT weapon, "arm_left".
  const auto kept = vs_scoobees_from_json(json::parse(R"([{"name": "OLD", "parts": {
      "arm_left": {"id": 4, "level": 1}, "shoulder_right": {"id": 9, "level": 0}}}])"));
  REQUIRE(kept.size() == 1);
  CHECK(kept[0].robot.part(PartSlot::right_arm)->id == 4);
  CHECK(kept[0].robot.part(PartSlot::left_shoulder)->id == 9);
  CHECK(!kept[0].robot.part(PartSlot::left_arm).has_value());
  const json::JsonValue saved = vs_scoobees_to_json(kept);
  CHECK(saved.array()[0].find("parts")->find("right_arm") != nullptr);
}
