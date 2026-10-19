// The battle chatter (0x049470, 0x049552): gates, chances and the most
// urgent event, the speaker's names and faces in the line, and the events
// the battle raises.

#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "phantomcrash/battle.hpp"
#include "phantomcrash/battle_chatter.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json.hpp"

using namespace phantomcrash;

namespace {

constexpr float tick = 1.0F / 60.0F;

// One line table whose every event says script 0: a name, a face and a
// line naming the pilot.
ChatterTables test_tables() {
  std::string gates = "[2, 1"; // as the game's: event 1 only for a player's robot
  std::string chances = "[1, 1";
  std::string table = "[0, 0";
  for (int event = 2; event < chatter_event::count; ++event) {
    gates += ", 0";
    chances += event == 9 ? ", 0" : ", 1"; // event 9 never passes
    table += ", 0";
  }
  const std::string document = R"({"gates": )" + gates + R"(], "chances": {"player": )" + chances +
                               R"(], "team_two": )" + chances + R"(], "computer": )" + chances +
                               R"(]}, "scripts": [[
      {"op": "sprite", "sprite": -1, "position": [173, 197], "layer": 275},
      {"op": "sprite", "sprite": -2, "position": [10, 10], "layer": 275},
      {"op": "text", "text": "{code:fff8}", "position": [242, 197]},
      {"op": "text", "text": "A{code:fff9}", "position": [242, 222]},
      {"op": "hold", "frames": 16}
    ]], "tables": [)" + table + "]]}";
  return parse_chatter_tables(json::parse(document));
}

GlyphFont test_font() {
  GlyphFont font;
  font.glyphs = {{U'Ａ', 1}, {U'Ｂ', 2}, {U'Ｃ', 3}};
  return font;
}

ChatterSpeaker test_speaker(const ChatterKind kind) {
  ChatterSpeaker speaker;
  speaker.table = 0;
  speaker.kind = kind;
  speaker.robot_name = "AB";
  speaker.pilot_name = "C";
  Sprite face;
  face.image = "textures/face.png";
  face.rect = {0.0F, 128.0F, 64.0F, 192.0F};
  speaker.robot_face = face;
  return speaker;
}

int glyphs(const EventText &text) {
  return static_cast<int>(std::ranges::count_if(
      text.tokens, [](const TextToken &token) { return token.kind == TextToken::Kind::glyph; }));
}

// The chatter events the battle raises over `ticks` ticks, as robot * 100 + event.
std::vector<int> chatter_events(Battle &battle, const FireControl &first, const int ticks) {
  std::vector<int> out;
  for (int index = 0; index < ticks; ++index) {
    battle.tick({}, index == 0 ? first : FireControl{}, tick);
    for (const BattleEvent &event : battle.events()) {
      if (event.kind == BattleEventKind::chatter) {
        out.push_back(event.robot * 100 + event.chatter);
      }
    }
  }
  return out;
}

bool has(const std::vector<int> &events, const int robot, const int event) {
  return std::ranges::find(events, robot * 100 + event) != events.end();
}

} // namespace

TEST_CASE("the most urgent passing event is said and the rest forgotten") {
  const ChatterTables tables = test_tables();
  BattleChatter chatter(tables);
  chatter.set_speaker(0, test_speaker(ChatterKind::player));
  chatter.set_speaker(1, test_speaker(ChatterKind::computer));
  chatter.raise(1, chatter_event::dodge);
  chatter.raise(0, chatter_event::critical_hit);
  chatter.raise(1, chatter_event::hit_from_behind);
  chatter.raise(0, 9);                         // its chance is 0
  chatter.raise(1, chatter_event::first_kill); // only a player's robot's
  chatter.raise(0, chatter_event::count);      // no such event
  const auto line = chatter.take(false);
  CHECK(line.has_value());
  if (line) {
    CHECK(line->robot == 1);
    CHECK(line->event == chatter_event::hit_from_behind);
    CHECK(line->script == 0);
  }
  CHECK(!chatter.take(false).has_value()); // the rest were forgotten
}

TEST_CASE("a line showing lets only a kill or event 4 through") {
  const ChatterTables tables = test_tables();
  BattleChatter chatter(tables);
  chatter.set_speaker(0, test_speaker(ChatterKind::player));
  chatter.raise(0, chatter_event::dodge);
  CHECK(!chatter.take(true).has_value());
  CHECK(!chatter.take(false).has_value()); // forgotten, not kept for later
  chatter.raise(0, chatter_event::first_kill);
  CHECK(chatter.take(true).has_value());
  ChatterSpeaker silent = test_speaker(ChatterKind::player);
  silent.table = -1;
  chatter.set_speaker(0, silent);
  chatter.raise(0, chatter_event::dodge);
  CHECK(!chatter.take(false).has_value());
}

TEST_CASE("in a scene only robot 0's entrance and sign-off lines play") {
  const ChatterTables tables = test_tables();
  BattleChatter chatter(tables);
  chatter.set_speaker(0, test_speaker(ChatterKind::player));
  chatter.set_speaker(1, test_speaker(ChatterKind::computer));
  chatter.set_scene(true);
  chatter.raise(0, chatter_event::dodge);
  chatter.raise(1, chatter_event::entrance); // not robot 0's
  CHECK(!chatter.take(false).has_value());
  chatter.raise(0, chatter_event::entrance);
  const auto line = chatter.take(false);
  CHECK(line.has_value() && line->event == chatter_event::entrance);
  chatter.raise(0, chatter_event::entrance);
  CHECK(!chatter.take(true).has_value()); // the entrance line doesn't close one showing
  chatter.raise(0, chatter_event::left);
  CHECK(chatter.take(true).has_value()); // the sign-off does
  chatter.set_scene(false);
  chatter.raise(0, chatter_event::dodge);
  CHECK(chatter.take(false).has_value());
}

TEST_CASE("a line prints the speaker's names and shows its faces") {
  const ChatterTables tables = test_tables();
  const ChatterSpeaker speaker = test_speaker(ChatterKind::player);
  const auto commands = chatter_script(tables, {0, chatter_event::dodge, 0}, speaker, test_font());
  CHECK(commands.size() == 4); // no pilot face: its sprite is left out
  if (commands.size() != 4) {
    return;
  }
  CHECK(commands[0].sprite.image == "textures/face.png");
  CHECK(commands[0].sprite.position[0] == 173.0F);
  CHECK(commands[0].sprite.layer == 275);
  CHECK(commands[0].sprite.rect[1] == 128.0F);
  CHECK(glyphs(commands[1].text) == 2); // "AB"
  CHECK(commands[1].text.tokens[1].glyph == 2);
  CHECK(glyphs(commands[2].text) == 2); // "A" and "C"
  CHECK(commands[2].text.tokens[1].glyph == 3);
}

TEST_CASE("the battle raises chatter for dodges, hits from behind and kills") {
  const auto settings = parse_battle_settings(json::parse(R"({
    "arena": {"half_size": 100},
    "weapons": {"handgun": {"kind": "gun", "damage": 40, "cooldown": 5, "speed": 300}}
  })"));
  FireControl fire;
  fire.fire[0] = true;

  Battle dodging(settings, EnemyPath{});
  dodging.add_robot("player", 0, {}, 0.0F, {"handgun", "", "", ""}, 1000.0F);
  const int nimble = dodging.add_robot("target", 1, Vec3{0.0F, 0.0F, -30.0F}, pi, {}, 1000.0F);
  CombatStats stats;
  stats.dodge_chance = 1.0F;
  dodging.set_combat(nimble, stats);
  CHECK(has(chatter_events(dodging, fire, 30), nimble, chatter_event::dodge));

  Battle behind(settings, EnemyPath{});
  behind.add_robot("player", 0, {}, 0.0F, {"handgun", "", "", ""}, 1000.0F);
  const int turned = behind.add_robot("target", 1, Vec3{0.0F, 0.0F, -30.0F}, 0.0F, {}, 1000.0F);
  const auto from_behind = chatter_events(behind, fire, 30);
  CHECK(has(from_behind, turned, chatter_event::hit_from_behind));
  CHECK(!has(from_behind, turned, chatter_event::half)); // 120 of 1000

  Battle killing(settings, EnemyPath{});
  killing.add_robot("player", 0, {}, 0.0F, {"handgun", "", "", ""}, 1000.0F);
  const int frail = killing.add_robot("target", 1, Vec3{0.0F, 0.0F, -30.0F}, pi, {}, 40.0F);
  const auto kill = chatter_events(killing, fire, 30);
  CHECK(has(kill, frail, chatter_event::quarter));
  CHECK(has(kill, frail, chatter_event::shot_down));
  CHECK(has(kill, 0, chatter_event::kill)); // not a rival: no first-kill line
  CHECK(killing.robots().front().piloted);
}

TEST_CASE("the battle raises chatter for rounds running low, firing and standing idle") {
  const auto settings = parse_battle_settings(json::parse(R"({
    "arena": {"half_size": 100}, "idle_chatter_seconds": 1,
    "weapons": {"handgun": {"kind": "gun", "damage": 1, "cooldown": 0.01, "speed": 300,
                            "ammo": 4},
                "launcher": {"kind": "gun", "damage": 1, "cooldown": 5, "speed": 300,
                             "hud_kind": 1, "ammo": 8}}
  })"));
  FireControl fire;
  fire.fire[0] = true;
  Battle shooting(settings, EnemyPath{});
  shooting.add_robot("player", 0, {}, 0.0F, {"handgun", "launcher", "", ""}, 1000.0F);
  std::vector<int> events;
  for (int index = 0; index < 10; ++index) {
    shooting.tick({}, fire, tick);
    for (const BattleEvent &event : shooting.events()) {
      if (event.kind == BattleEventKind::chatter) {
        events.push_back(event.robot * 100 + event.chatter);
      }
    }
  }
  // Four of four rounds and eight of eight make 1; the handgun spent, the
  // share is a half.
  CHECK(has(events, 0, chatter_event::ammo_half));
  FireControl launch;
  launch.fire[1] = true;
  CHECK(has(chatter_events(shooting, launch, 1), 0, chatter_event::weapon_fired));

  Battle idle(settings, EnemyPath{});
  idle.add_robot("player", 0, {}, 0.0F, {}, 1000.0F);
  CHECK(!has(chatter_events(idle, FireControl{}, 50), 0, chatter_event::idle));
  CHECK(has(chatter_events(idle, FireControl{}, 20), 0, chatter_event::idle));
}

TEST_CASE("the converted chatter reads, when the game files are converted") {
  const auto file = DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).find_asset("tables/chatter.json");
  if (!file) {
    return; // not converted here
  }
  const ChatterTables tables = parse_chatter_tables(json::read_file(*file));
  CHECK(tables.tables.size() == 67);
  CHECK(tables.gates[chatter_event::first_kill] == 1);
  CHECK(tables.chances[0][chatter_event::critical_hit] == 0.5F);
  // Table 0, the chips' report voice: a line for the first kill.
  const int script = tables.tables[0][chatter_event::first_kill];
  CHECK(script >= 0);
  const auto commands = chatter_script(tables, {0, chatter_event::first_kill, script},
                                       test_speaker(ChatterKind::player), GlyphFont{});
  CHECK(commands.size() >= 4);
}
