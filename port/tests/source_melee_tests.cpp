#include "test_support.hpp"
#include "phantomcrash/battle_sounds.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/rigid_animation.hpp"
#include "phantomcrash/robot_combat.hpp"
#include "phantomcrash/robot_library.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/test_drive.hpp"
#include <algorithm>
#include <cmath>

using namespace phantomcrash;

namespace {
constexpr float tick = 1.0F / 60;
BattleSettings settings(int source_kind) {
  BattleSettings out;
  WeaponTuning weapon;
  weapon.name = "blade";
  weapon.kind = WeaponKind::melee;
  weapon.hud_kind = hud_kind::melee;
  weapon.model = 300;
  weapon.source_melee_kind = source_kind;
  weapon.damage = 0;
  weapon.max_ammo = 1000;
  out.weapons.emplace(weapon.name, weapon);
  return out;
}
struct Fixture {
  Battle battle;
  BattleSounds sounds{parse_battle_sounds(json::parse(std::string_view(R"({
    "fire":[{"models":[300],"sound":"177","trigger_policy":"shot",
             "requires_free_arm_action":true}]})")))};
  explicit Fixture(int source_kind = 6) : battle(settings(source_kind), {}) {
    battle.add_robot("one", 0, {}, 0, {"blade", "blade", "", ""}, 100);
  }
  std::vector<PlacedCue> fire(int slot = 0) {
    FireControl control;
    control.fire[slot] = true;
    battle.tick({}, control, tick);
    sounds.observe(battle, tick);
    return sounds.take();
  }
};
int cue_count(const std::vector<PlacedCue> &cues) {
  return static_cast<int>(std::ranges::count_if(cues, [](const auto &cue) {
    return cue.cue.action == SoundCue::Action::play && cue.cue.name == "177";
  }));
}
int part_id(const json::JsonValue &manifest, const std::string &name) {
  for (const auto &part : manifest.find("parts")->array()) {
    if (part.string_or("model", "") == name) {
      return static_cast<int>(part.number_or("part", -1));
    }
  }
  return -1;
}
} // namespace

TEST_CASE("imported free and locked melee families remain melee in shop records") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  if (!roots.find_asset("slai/native_parts.json")) {
    return;
  }
  const auto parts = parse_part_tables(json::read_file(roots.asset("tables/parts.json")));
  const auto drive = parse_test_drive(roots.document("world/test_drive.json"));
  int source_melee = 0;
  for (const auto &part : parts.arm_weapons) {
    if (part.source_melee_kind == 6 || part.source_melee_kind == 7) {
      CHECK(part.kind == hud_kind::melee);
      const auto weapon =
          record_weapon(drive, {}, parts, {WeaponTable::arm, part.index, 0}, "blade");
      REQUIRE(weapon.has_value());
      CHECK(weapon->kind == WeaponKind::melee);
      CHECK(weapon->source_melee_kind == part.source_melee_kind);
      ++source_melee;
    }
  }
  CHECK(source_melee == 6);
}

TEST_CASE("source free arm sound follows accepted actions on either side") {
  for (int slot = 0; slot < 2; ++slot) {
    Fixture f;
    CHECK(cue_count(f.fire(slot)) == 1);
    CHECK(f.battle.player().source_free_arm_action);
    CHECK(cue_count(f.fire(slot)) == 0); // already striking
    for (int frame = 0; frame < 120; ++frame) {
      f.battle.tick({}, {}, tick);
    }
    CHECK(!f.battle.player().source_free_arm_action);
  }
  Fixture cooling;
  cooling.battle.robot(0).cooldown.fill(1);
  CHECK(cue_count(cooling.fire()) == 0);
  CHECK(!cooling.battle.player().source_free_arm_action);
  Fixture blocked;
  blocked.battle.robot(0).mover.stagger(1, {});
  CHECK(cue_count(blocked.fire()) == 0);
  CHECK(!blocked.battle.player().source_free_arm_action);
  Fixture legacy(-1);
  CHECK(cue_count(legacy.fire()) == 0);
}

TEST_CASE("completed source lock selects a lunge and suppresses free arm cue") {
  for (const float lock_seconds : {0.0F, 1.0F}) {
    Fixture f;
    f.battle.add_robot("target", 1, {0, 0, -12}, 0, {}, 100);
    f.battle.set_lock_seconds(0, lock_seconds);
    const auto cues = f.fire();
    REQUIRE(f.battle.player().locks[0] == 1);
    CHECK(cue_count(cues) == (lock_seconds == 0 ? 0 : 1));
    CHECK(f.battle.player().source_free_arm_action == (lock_seconds > 0));
  }
}

TEST_CASE("source body disabled actions retain animation and independent attacks own a clock") {
  RigidRig rig;
  rig.motion_clips.resize(45);
  rig.motion_clips[28] = "idle";
  rig.motion_clips[41] = "right_attack";
  rig.motion_clips[42] = "left_attack";
  rig.preserve_disabled_clips = true;
  rig.independent_motions = {37, 38, 39, 40, 41, 42, 43, 44};
  RigidPlayback state;
  state.update(rig, 28, 30, true, tick);
  CHECK(state.clip == "idle");
  CHECK(std::abs(state.seconds - .5F) < .00001F);
  state.update(rig, 37, 200, false, tick);
  CHECK(state.clip == "idle");
  CHECK(state.loop);
  CHECK(std::abs(state.seconds - (.5F + tick)) < .00001F);
  state.update(rig, 41, 200, false, tick);
  CHECK(state.clip == "right_attack");
  CHECK(state.seconds == 0);
  CHECK(!state.loop);
  state.update(rig, 41, 300, false, tick);
  CHECK(std::abs(state.seconds - tick) < .00001F);
  state.update(rig, 41, 300, false, tick, 1);
  CHECK(state.seconds == 0); // another accepted attack on the same side
  state.update(rig, 42, 300, false, tick);
  CHECK(state.clip == "left_attack");
  CHECK(state.seconds == 0);
  state.update(rig, 28, 60, true, tick);
  CHECK(state.clip == "idle");
  CHECK(state.seconds == 1);
}

TEST_CASE("imported arm actions select authored family side and completed lock") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto manifest_file = roots.find_asset("slai/native_parts.json");
  if (!manifest_file) {
    return;
  }
  const auto manifest = json::read_file(*manifest_file);
  RobotLibrary library(roots);
  RobotParts parts;
  parts.body = part_id(manifest, "S1B01H");
  parts.legs = part_id(manifest, "S1L01H");
  REQUIRE(parts.body >= 0);
  REQUIRE(parts.legs >= 0);
  const auto assembly = assemble_robot(library.catalogue(), library.skeletons(), parts);
  for (const int family : {6, 7}) {
    for (int slot = 0; slot < 2; ++slot) {
      for (const bool locked : {false, true}) {
        Fixture f(family);
        f.battle.robot(0).pose = std::make_shared<RobotCombat>(library, assembly);
        if (locked) {
          f.battle.add_robot("target", 1, {0, 0, -12}, 0, {}, 100);
          f.battle.set_lock_seconds(0, 0);
        }
        for (int frame = 0; frame < 60 && !f.battle.player().mover.striking(); ++frame) {
          f.fire(slot); // the native arm must finish raising before accepting the trigger
        }
        const int action = (family == 6 ? 39 : 35) + (locked ? 2 : 0) + slot;
        REQUIRE(f.battle.player().mover.striking());
        CHECK(f.battle.player().mover.motion() == action + 2);
      }
    }
  }
}
