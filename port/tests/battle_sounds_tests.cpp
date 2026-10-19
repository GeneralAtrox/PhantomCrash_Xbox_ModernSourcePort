// The battle's sounds (battle_sounds.hpp): the weapons' rules, the robots'
// own sounds and their placing.

#include "test_support.hpp"

#include "phantomcrash/battle_sounds.hpp"
#include "phantomcrash/json.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace phantomcrash;

namespace {

constexpr float tick = 1.0F / 60.0F;

BattleSoundTable test_table() {
  return parse_battle_sounds(json::parse(R"({
    "robot": {
      "footstep": {"player": "p_step", "computer": "c_step"},
      "boost_start": {"player": "p_boost_start"},
      "boost": {"player": "p_boost"},
      "shot_down": {"sound": "down"}
    },
    "footstep_seconds": 0.25,
    "fire": [
      {"style": 0, "mount": "shoulder", "models": [1, 11], "player": "p_shoulder", "computer": "c_shoulder"},
      {"style": 2, "hud_kind": 3, "player": "p_grenade"},
      {"style": 2, "player": "p_missile"},
      {"style": 0, "player": "p_gun"}
    ],
    "fire_fallback": {"player": "p_plain", "computer": "c_plain"},
    "impact": [{"hud_kind": 5}, {"sound": "impact"}]
  })"));
}

BattleSettings test_settings() {
  return parse_battle_settings(json::parse(R"({
    "arena": {"half_size": 100},
    "weapons": {"handgun": {"kind": "gun", "damage": 40, "cooldown": 0.1, "speed": 300}}
  })"));
}

std::vector<std::string> names(const std::vector<PlacedCue> &cues, SoundCue::Action action) {
  std::vector<std::string> out;
  for (const PlacedCue &placed : cues) {
    if (placed.cue.action == action) {
      out.push_back(placed.cue.name);
    }
  }
  return out;
}

bool has(const std::vector<std::string> &list, const std::string &name) {
  return std::ranges::find(list, name) != list.end();
}

} // namespace

TEST_CASE("a weapon's fire sound is the first rule its record matches") {
  const BattleSoundTable table = test_table();
  WeaponTuning weapon;
  weapon.fire_sound = 0;
  weapon.model = 11;
  const int shoulder = static_cast<int>(WeaponSlot::left_shoulder);
  const int arm = static_cast<int>(WeaponSlot::right_arm);
  CHECK(weapon_sound(table.fire, weapon, shoulder)->player == "p_shoulder");
  CHECK(weapon_sound(table.fire, weapon, arm)->player == "p_gun"); // the mount differs
  weapon.fire_sound = 2;
  weapon.hud_kind = hud_kind::grenade;
  CHECK(weapon_sound(table.fire, weapon, arm)->player == "p_grenade");
  weapon.hud_kind = hud_kind::missile;
  CHECK(weapon_sound(table.fire, weapon, arm)->player == "p_missile");
  weapon.fire_sound = 5;
  CHECK(weapon_sound(table.fire, weapon, arm) == nullptr);
  weapon.hud_kind = hud_kind::beam;
  CHECK(weapon_sound(table.impact, weapon, -1)->player.empty()); // a beam lands silently
}

TEST_CASE(
    "beam audio starts once while held and stops on release, weapon loss, empty charge or death") {
  for (int ending = 0; ending < 4; ++ending) {
    BattleSettings settings;
    WeaponTuning beam;
    beam.kind = WeaponKind::gun;
    beam.hud_kind = hud_kind::beam;
    beam.fire_sound = 5;
    beam.max_ammo = 100;
    beam.cooldown = .1F;
    settings.weapons.emplace("beam", beam);
    Battle battle(settings, {});
    battle.add_robot("player", 0, {}, 0, {"beam", "", "", ""}, 100);
    BattleSounds sounds(parse_battle_sounds(
        json::parse(R"({"fire":[{"style":5,"sound":"beam_sound"}],"beam_restart_seconds":9})")));
    FireControl fire;
    fire.fire[0] = true;
    battle.tick({}, fire, tick);
    sounds.observe(battle, tick);
    const auto started = sounds.take();
    REQUIRE(started.size() == 1);
    CHECK(started[0].cue.name == "beam_sound");
    const auto key = started[0].cue.key;
    CHECK(key != 0);
    for (int i = 0; i < 12; ++i) {
      battle.tick({}, fire, tick);
      sounds.observe(battle, tick);
      const auto held = sounds.take();
      REQUIRE(held.size() == 1);
      CHECK(held[0].cue.action == SoundCue::Action::move);
      CHECK(held[0].cue.key == key);
    }
    if (ending == 0) {
      fire.fire[0] = false;
    }
    if (ending == 1) {
      battle.robot(0).lost[0] = true;
    }
    if (ending == 2) {
      battle.robot(0).magazines[0].ammo = 0;
    }
    if (ending == 3) {
      battle.robot(0).alive = false;
    }
    battle.tick({}, fire, tick);
    sounds.observe(battle, tick);
    const auto stopped = sounds.take();
    REQUIRE(stopped.size() == 1);
    CHECK(stopped[0].cue.action == SoundCue::Action::stop);
    CHECK(stopped[0].cue.key == key);
    battle.tick({}, fire, tick);
    sounds.observe(battle, tick);
    CHECK(sounds.take().empty());
  }
}

TEST_CASE("a battle sounds its shots where they leave and land, and a walk's steps") {
  Battle battle(test_settings(), EnemyPath{});
  battle.add_robot("player", 0, {}, 0.0F, {"handgun", "", "", ""}, 100.0F);
  const int target = battle.add_robot("target", 1, {0.0F, 0.0F, -30.0F}, 0.0F, {}, 1000.0F);
  BattleSounds sounds(test_table());
  FireControl fire;
  fire.fire[0] = true;
  std::vector<PlacedCue> heard;
  for (int i = 0; i < 20; ++i) {
    battle.tick({}, fire, tick);
    sounds.observe(battle, tick);
    std::ranges::move(sounds.take(), std::back_inserter(heard));
  }
  const auto played = names(heard, SoundCue::Action::play);
  CHECK(has(played, "p_plain")); // a weapon without a record: the fallback, the player's side
  CHECK(has(played, "impact"));
  const auto shot = std::ranges::find(heard, std::string("p_plain"),
                                      [](const PlacedCue &placed) { return placed.cue.name; });
  CHECK(length(shot->at - battle.muzzle(battle.player(), WeaponSlot::right_arm)) < 1.0F);
  CHECK(battle.robots()[static_cast<std::size_t>(target)].hp < 1000.0F);

  heard.clear();
  RobotControl walk;
  walk.move = {0.0F, 0.7F};       // at the dash's threshold: still a walk
  for (int i = 0; i < 120; ++i) { // two seconds at 0.7 of the walk: 5.6 steps
    battle.tick(walk, {}, tick);
    sounds.observe(battle, tick);
    std::ranges::move(sounds.take(), std::back_inserter(heard));
  }
  CHECK(battle.player().mover.mode() == MoveMode::walk);
  const auto steps = std::ranges::count(names(heard, SoundCue::Action::play), "p_step");
  CHECK(steps >= 5);
  CHECK(steps <= 6);
}

TEST_CASE("a dash starts its sound, loops another with the robot and stops it") {
  Battle battle(test_settings(), EnemyPath{});
  battle.add_robot("player", 0, {}, 0.0F, {}, 100.0F);
  BattleSounds sounds(test_table());
  RobotControl dash;
  dash.move = {0.0F, 1.0F};
  dash.dash = true;
  battle.tick(dash, {}, tick);
  sounds.observe(battle, tick);
  std::vector<PlacedCue> heard = sounds.take();
  REQUIRE(battle.player().mover.mode() == MoveMode::dash);
  CHECK(has(names(heard, SoundCue::Action::play), "p_boost_start"));
  const auto loop = std::ranges::find(heard, std::string("p_boost"),
                                      [](const PlacedCue &placed) { return placed.cue.name; });
  REQUIRE(loop != heard.end());
  CHECK(loop->cue.loop);
  const std::uint64_t key = loop->cue.key;
  CHECK(key != 0);
  dash.dash = false;
  battle.tick(dash, {}, tick);
  sounds.observe(battle, tick);
  heard = sounds.take();
  CHECK(std::ranges::any_of(heard, [&](const PlacedCue &placed) {
    return placed.cue.action == SoundCue::Action::move && placed.cue.key == key;
  }));
  battle.tick({}, {}, tick);
  sounds.observe(battle, tick);
  heard = sounds.take();
  CHECK(std::ranges::any_of(heard, [&](const PlacedCue &placed) {
    return placed.cue.action == SoundCue::Action::stop && placed.cue.key == key;
  }));
}

TEST_CASE("placed cues fade with the camera's distance; the split screen hears them whole") {
  std::vector<PlacedCue> cues{{SoundCue{SoundCue::Action::play, "far"}, {0.0F, 0.0F, -200.0F}}};
  const SoundListener listener;
  CHECK(place_cues(SoundFalloff{}, &listener, cues).front().mix.gain < 0.1F);
  CHECK(place_cues(SoundFalloff{}, nullptr, cues).front().mix.gain == 1.0F);
  CHECK(place_cues(SoundFalloff{}, &listener, cues).front().position.has_value());
  CHECK(!place_cues(SoundFalloff{}, nullptr, cues).front().position.has_value());
  const auto silent_tick = place_cues(SoundFalloff{}, &listener, {});
  REQUIRE(silent_tick.size() == 1);
  CHECK(silent_tick.front().action == SoundCue::Action::listener);
}

TEST_CASE("a landing on surfaces 5 to 7 plays their sound, elsewhere the robot's own") {
  const BattleSoundTable table = parse_battle_sounds(json::parse(R"({"robot": {
    "landing": {"player": "own.wav", "computer": "computer.wav"},
    "landing_surfaces": [{"surfaces": [5, 7], "sound": "hard.wav"},
                         {"surfaces": [6], "sound": "soft.wav"}]}})"));
  Combatant robot{MovementTuning{}};
  robot.piloted = true;
  robot.floor_surface = 9; // the training ground's floor
  CHECK(landing_sound(table, robot) == "own.wav");
  robot.floor_surface = 7;
  CHECK(landing_sound(table, robot) == "hard.wav");
  robot.floor_surface = 1006; // surface % 1000
  CHECK(landing_sound(table, robot) == "soft.wav");
  robot.floor_surface = -1;
  robot.stage_contact = surface_flags(5); // touched as it landed
  CHECK(landing_sound(table, robot) == "hard.wav");
  robot.stage_contact = 0;
  robot.piloted = false;
  CHECK(landing_sound(table, robot) == "computer.wav");
  const auto files = sound_files(table);
  CHECK(std::ranges::find(files, "soft.wav") != files.end());
}
