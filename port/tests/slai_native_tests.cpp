#include "test_support.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/robot.hpp"
#include "phantomcrash/training_ground.hpp"
#include "phantomcrash/test_drive.hpp"
#include "phantomcrash/world_model.hpp"
#include "phantomcrash/vs_mode_script.hpp"
#include "phantomcrash/battle_sounds.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/battle_weapons.hpp"
#include "phantomcrash/battle.hpp"

#include <cmath>
#include <set>

using namespace phantomcrash;

namespace {
void check_model(const DataRoots &roots, const std::string &path) {
  const auto model = load_model(roots.asset(path), path);
  REQUIRE(!model.vertices.empty());
  REQUIRE(!model.draws.empty());
  for (const auto &texture : model.textures) {
    REQUIRE(roots.find_asset(texture.image).has_value());
  }
  for (const auto &draw : model.draws) {
    REQUIRE(draw.node >= 0 && draw.node < static_cast<int>(model.nodes.size()));
    REQUIRE(draw.first_index + draw.index_count <= model.indices.size());
  }
}

void check_pose(const RobotAssembly &assembly, const Motion *motion) {
  REQUIRE(assembly.skeleton != nullptr);
  PoseInput input;
  input.legs = {motion, motion ? motion->frames * 0.5F : 0};
  input.arm_skeletons = assembly.arm_skeletons;
  Pose pose;
  build_pose(*assembly.skeleton, input, pose);
  REQUIRE(pose[0].set[0]);
  for (const auto &part : pose) {
    for (std::size_t bone = 0; bone < part.set.size(); ++bone) {
      if (part.set[bone]) {
        for (int r = 0; r < 4; ++r) {
          for (int c = 0; c < 4; ++c) {
            CHECK(std::isfinite(part.bones[bone].at(r, c)));
          }
        }
      }
    }
  }
}
} // namespace

TEST_CASE("native S.L.A.I. parts assemble with both games and load all paints and motions") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto manifest = roots.find_asset("slai/native_parts.json");
  if (!manifest) {
    return;
  }
  const auto catalogue =
      parse_robot_catalogue(json::read_file(roots.asset("robots/catalogue.json")));
  const auto skeletons =
      parse_robot_skeletons(json::read_file(roots.asset("robots/skeletons.json")));
  const auto list = json::read_file(*manifest);
  REQUIRE(list.find("unresolved")->array().empty());
  std::set<std::string> models;
  for (const auto &entry : list.find("parts")->array()) {
    const auto category = entry.string_or("category", "");
    const int id = static_cast<int>(entry.number_or("part", -1));
    RobotParts parts;
    parts.body = 0;
    parts.legs = 0;
    if (category == "body") {
      parts.body = id;
    } else if (category == "leg") {
      parts.legs = id;
    } else if (category == "arm") {
      parts.arms = {id, id};
    } else if (category == "shoulder") {
      parts.shoulders = {id, id};
    }
    for (int colour : {0, 57, 114}) {
      parts.colour = colour;
      const auto assembly = assemble_robot(catalogue, skeletons, parts);
      for (const auto &paint : assembly.part_paints) {
        if (!paint.empty()) {
          CHECK(roots.find_asset(paint).has_value());
        }
      }
      models.insert(assembly.body_model);
      models.insert(assembly.legs_model);
      for (const auto &path : assembly.arm_models) {
        if (!path.empty()) {
          models.insert(path);
        }
      }
      for (const auto &path : assembly.shoulder_models) {
        if (!path.empty()) {
          models.insert(path);
        }
      }
      check_pose(assembly, nullptr);
      if (category == "leg" && !assembly.leg_motions.empty()) {
        const auto bank = parse_motion_bank(json::read_file(roots.asset(assembly.leg_motions)));
        for (const auto &motion : bank.motions) {
          if (motion) {
            check_pose(assembly, &*motion);
          }
        }
      }
    }
  }
  for (const auto &path : models) {
    check_model(roots, path);
  }
}

TEST_CASE("S.L.A.I. weapon fire rules select the recovered cues for both mounts") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto file = roots.find_asset("slai/sound_cues.json");
  if (!file) {
    return;
  }
  const auto cues = json::read_file(*file);
  const auto imported = json::read_file(roots.asset("slai/native_parts.json"));
  const auto sounds = parse_battle_sounds(roots.document("audio/battle_sounds.json"));
  for (const auto *list : {cues.find("bound_parts"), cues.find("silent_parts")}) {
    for (const auto &entry : list->array()) {
      const auto name = entry.string_or("model", "");
      const auto &parts = imported.find("parts")->array();
      const auto part = std::find_if(parts.begin(), parts.end(), [&](const auto &p) {
        return p.string_or("model", "") == name;
      });
      REQUIRE(part != parts.end());
      WeaponTuning weapon;
      weapon.model = static_cast<int>(part->number_or("type", -1));
      const bool shoulder = part->string_or("category", "") == "shoulder";
      const auto *pair = weapon_sound(sounds.fire, weapon, shoulder ? 2 : 0);
      REQUIRE(pair != nullptr);
      CHECK(pair->player == pair->computer);
      if (entry.number_or("cue", -1) >= 0) {
        REQUIRE(roots.find_asset(pair->player).has_value());
      } else {
        CHECK(pair->player.empty());
      }
    }
  }
}

TEST_CASE("selectable S.L.A.I. arenas load geometry resources and standing collision") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto manifest = roots.find_asset("slai/native_stages.json");
  if (!manifest) {
    return;
  }
  const auto stages = parse_stages(roots.document("world/stages.json"));
  const auto choices = parse_test_drive(roots.document("world/test_drive.json"));
  const auto list = json::read_file(*manifest);
  const auto menu = parse_vs_mode(roots.document("ui/vs_mode.json"));
  REQUIRE(menu.area.items.size() > list.find("stages")->array().size());
  for (const auto &entry : list.find("stages")->array()) {
    const auto name = entry.string_or("stage", "");
    const auto &stage = stages.at(name);
    REQUIRE(stage.spawn.has_value());
    check_model(roots, stage.model);
    const auto collision = json::read_file(roots.asset(stage.collision));
    REQUIRE(!collision.find("floor")->array().empty());
    auto hit = parse_stage_collision(collision);
    const auto spawn = *stage.spawn;
    const float ground = hit.ground_height(spawn, .1F);
    CHECK(ground != no_ground);
    CHECK(std::abs(ground - spawn.y) < .1F);
    Battle battle(BattleSettings{}, EnemyPath{});
    battle.set_stage(std::move(hit));
    const int rider = battle.add_robot("stage start", 0, spawn, 0, {}, 1000);
    const auto placed = battle.robots()[rider].mover.position();
    CHECK(std::isfinite(placed.y));
    CHECK(placed.y - spawn.y < 4.5F);
    CHECK(std::any_of(choices.stage_choices.begin(), choices.stage_choices.end(),
                      [&](const auto &choice) { return choice.first == name; }));
  }
}
