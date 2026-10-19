#include "test_support.hpp"
#include "phantomcrash/battle_sounds.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/rigid_animation.hpp"
#include "phantomcrash/robot.hpp"
#include "phantomcrash/source_particles.hpp"
#include "phantomcrash/training_ground.hpp"
#include "phantomcrash/world_model.hpp"
#include <cmath>

using namespace phantomcrash;

namespace {
void check_fronts(const ModelData &model) {
  for (const auto &draw : model.draws) {
    if (model.materials[static_cast<std::size_t>(draw.material)].unlit) {
      continue;
    }
    const auto &world = model.nodes[static_cast<std::size_t>(draw.node)].world;
    const Vec3 x{world.at(0, 0), world.at(0, 1), world.at(0, 2)};
    const Vec3 y{world.at(1, 0), world.at(1, 1), world.at(1, 2)};
    const Vec3 z{world.at(2, 0), world.at(2, 1), world.at(2, 2)};
    const float orientation = dot(x, cross(y, z)) < 0 ? -1.0F : 1.0F;
    for (std::uint32_t i = draw.first_index; i < draw.first_index + draw.index_count; i += 3) {
      std::array<Vec3, 3> p;
      Vec3 normal{};
      for (std::size_t j = 0; j < 3; ++j) {
        const auto &v = model.vertices[model.indices[i + j]];
        p[j] = {v.position[0], v.position[1], v.position[2]};
        normal = normal + Vec3{v.normal[0], v.normal[1], v.normal[2]};
      }
      CHECK(dot(cross(p[1] - p[0], p[2] - p[0]), normal) * orientation >= -1e-7F);
    }
  }
}
} // namespace

TEST_CASE("rigid animation keeps parent-only nodes and resets channels between clips") {
  ModelData model;
  auto rig = std::make_shared<RigidRig>();
  rig->nodes.resize(2);
  rig->nodes[0].local = translation({4, 0, 0});
  rig->nodes[1].local = translation({0, 3, 0});
  rig->nodes[1].parent = 0;
  rig->arm_mount_nodes = {-1, 1};
  rig->shoulder_mount_nodes = {1, 0};
  RigidChannel c;
  c.node = 0;
  c.path = RigidChannel::Path::position;
  c.times = {0, 1};
  c.values = {{{0, 0, 0, 0}}, {{2, 0, 0, 0}}};
  rig->clips.push_back({"move", 1, {c}});
  model.rig = rig;
  ModelNode node;
  node.rig_node = 1;
  model.nodes.push_back(node);
  std::vector<Mat4> posed;
  std::array<Mat4, 2> arms, shoulders;
  pose_rigid_model(model, "move", .5F, false, translation({10, 0, 0}), posed, &arms, -1, nullptr,
                   &shoulders);
  CHECK(std::abs(posed[0].at(3, 0) - 11) < .0001F);
  CHECK(std::abs(posed[0].at(3, 1) - 3) < .0001F);
  CHECK(arms[0].at(3, 0) == 10); // A disabled side does not borrow another attachment.
  CHECK(std::abs(arms[1].at(3, 0) - 11) < .0001F);
  CHECK(arms[1].at(3, 1) == 3);
  CHECK(shoulders[0].at(3, 1) == 3);
  CHECK(shoulders[1].at(3, 1) == 0);
  CHECK(std::abs(shoulders[1].at(3, 0) - 11) < .0001F);
  pose_rigid_model(model, "", 0, false, {}, posed);
  CHECK(posed[0].at(3, 0) == 4);
  pose_rigid_model(model, "move", 1.5F, true, {}, posed);
  CHECK(std::abs(posed[0].at(3, 0) - 1) < .0001F);
}

TEST_CASE("PTK emission stops in source ticks while existing particles finish") {
  SourceParticleDefinition d;
  SourceGenerator g;
  g.flags = 1 | 4;
  g.capacity = 2;
  g.rate = 1;
  g.stop = 2;
  g.lifetime = 4;
  g.drag = 1;
  d.generators.push_back(g);
  SourceParticles system(60, .1F);
  system.start(d, {1, 2, 3});
  system.tick(2.01F / 60);
  REQUIRE(system.emitters().size() == 1);
  CHECK(system.emitters()[0].particles.size() == 2);
  system.tick(6.01F / 60);
  CHECK(system.emitters().empty());
  g.flags = 1;
  g.stop = 0;
  d.generators[0] = g;
  system.start(d, {}, 42);
  system.start(d, {4, 0, 0}, 42);
  CHECK(system.emitters().size() == 1);
  CHECK(system.emitters()[0].parent.frame.at(3, 0) == 4);
  system.stop(42);
  system.tick(1.01F / 60);
  CHECK(system.emitters().empty());
}

TEST_CASE("source particle curves retain the recovered nonlinear branches") {
  CHECK(std::abs(source_particle_curve(2, 0, 8, .5F) - 2) < .0001F);
  CHECK(std::abs(source_particle_curve(3, 0, 16, .5F) - 15) < .0001F);
  CHECK(std::abs(source_particle_curve(4, 0, 1, .5F)) < .0001F);
  CHECK(source_particle_curve(0, 7, 12, 1) == 7);
}

TEST_CASE("held source weapon audio starts once, moves, and stops on release") {
  auto table = parse_battle_sounds(json::parse(std::string_view(
      R"({"fire":[{"mount":"arm","models":[300],"sound":"source.wav","loop":true,"trigger_policy":"held"}]})")));
  BattleSounds sounds(std::move(table));
  BattleSettings settings;
  WeaponTuning weapon;
  weapon.name = "source";
  weapon.model = 300;
  settings.weapons[weapon.name] = weapon;
  Battle battle(settings, {});
  const int id = battle.add_robot("source", 0, {}, 0, {"source", "", "", ""}, 100);
  battle.robot(id).firing[0] = true;
  sounds.observe(battle, .1F);
  auto cues = sounds.take();
  REQUIRE(cues.size() == 1);
  CHECK(cues[0].cue.action == SoundCue::Action::play);
  CHECK(cues[0].cue.loop);
  const auto key = cues[0].cue.key;
  sounds.observe(battle, .1F);
  cues = sounds.take();
  REQUIRE(cues.size() == 1);
  CHECK(cues[0].cue.action == SoundCue::Action::move);
  CHECK(cues[0].cue.key == key);
  battle.robot(id).firing[0] = false;
  sounds.observe(battle, .1F);
  cues = sounds.take();
  REQUIRE(cues.size() == 1);
  CHECK(cues[0].cue.action == SoundCue::Action::stop);
  CHECK(cues[0].cue.key == key);
}

TEST_CASE("S.L.A.I. rigid models and PTK pool load their playable data") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto file = roots.find_asset("slai/native_particles.json");
  if (!file) {
    return;
  }
  const auto particles = json::read_file(*file);
  REQUIRE(json::required(particles, "pool").array().size() == 60);
  std::size_t active = 0;
  for (const auto &entry : json::required(particles, "pool").array()) {
    const auto definition =
        parse_source_particles(json::read_file(roots.asset(entry.string_or("file", ""))));
    active += definition.generators.size();
    SourceParticles system(60, .1F);
    system.start(definition, {});
    system.tick(.5F);
    for (const auto &emitter : system.emitters()) {
      for (const auto &p : emitter.particles) {
        CHECK(std::isfinite(p.position.x));
        CHECK(std::isfinite(p.position.y));
        CHECK(std::isfinite(p.position.z));
      }
    }
  }
  CHECK(active > 100);
  const auto parts = json::read_file(roots.asset("slai/native_parts.json"));
  const auto catalogue =
      parse_robot_catalogue(json::read_file(roots.asset("robots/catalogue.json")));
  for (const auto &entry : json::required(parts, "parts").array()) {
    if (entry.string_or("category", "") == "leg") {
      continue;
    }
    const auto type = static_cast<std::size_t>(entry.number_or("type", -1));
    const auto category = entry.string_or("category", "");
    const int resource = category == "body"  ? catalogue.body_types.at(type).model
                         : category == "arm" ? catalogue.arm_types.at(type).models[0]
                                             : catalogue.shoulder_types.at(type).models[0];
    const auto path = "models/binlink/" + std::to_string(resource) + ".gltf";
    const auto model = load_model(roots.asset(path), path);
    REQUIRE(model.rig != nullptr);
    check_fronts(model);
    if (category != "body") {
      const int other = category == "arm" ? catalogue.arm_types.at(type).models[1]
                                          : catalogue.shoulder_types.at(type).models[1];
      const auto other_path = "models/binlink/" + std::to_string(other) + ".gltf";
      check_fronts(load_model(roots.asset(other_path), other_path));
    }
    std::vector<Mat4> pose;
    for (const auto &clip : model.rig->clips) {
      pose_rigid_model(model, clip.name, clip.duration * .5F, false, {}, pose);
      REQUIRE(pose.size() == model.nodes.size());
      for (const auto &m : pose) {
        for (float f : m.m) {
          CHECK(std::isfinite(f));
        }
      }
    }
  }
}

TEST_CASE("S.L.A.I. ballistic projectile models have outward native fronts") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto file = roots.find_asset("slai/native_particles.json");
  if (!file) {
    return;
  }
  const auto table = json::read_file(*file);
  const auto &routes = json::required(table, "projectiles").array();
  REQUIRE(!routes.empty());
  for (const auto &route : routes) {
    const auto path = route.string_or("file", "");
    const auto model = load_model(roots.asset(path), path);
    REQUIRE(!model.draws.empty());
    check_fronts(model);
    const int flight = static_cast<int>(route.number_or("source_flight", -1));
    CHECK((flight == 1 || flight == 5));
    CHECK(std::fabs(route.number_or("scale", 0) - (flight == 1 ? .03 : .018)) < 1e-7);
  }
}

TEST_CASE("source impact routing distinguishes robot and surface hits for both mounts") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  if (!roots.find_asset("slai/native_particles.json")) {
    return;
  }
  const auto table = parse_battle_sounds(roots.document("audio/battle_sounds.json"));
  const auto particles = json::read_file(roots.asset("slai/native_particles.json"));
  for (const auto &rule : json::required(particles, "impacts").array()) {
    const bool shoulder = rule.string_or("mount", "") == "shoulder";
    WeaponTuning weapon;
    weapon.model = static_cast<int>(rule.number_or("model", -1));
    weapon.part = PartRef{shoulder ? WeaponTable::shoulder : WeaponTable::arm, 0, 0};
    for (const bool robot : {true, false}) {
      const auto *sound = weapon_sound(table.impact, weapon, -1, robot);
      REQUIRE(sound != nullptr);
      CHECK(sound->player.starts_with("slai/"));
      CHECK(roots.find_asset(sound->player).has_value());
    }
  }
}

TEST_CASE("imported arena fronts face their authored normals in the native renderer") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  if (!roots.find_asset("slai/native_stages.json")) {
    return;
  }
  const auto catalogue = json::read_file(roots.asset("slai/native_stages.json"));
  const auto stages = parse_stages(roots.document("world/stages.json"));
  for (const auto &entry : catalogue.find("stages")->array()) {
    const auto &stage = stages.at(entry.string_or("stage", ""));
    check_fronts(load_model(roots.asset(stage.model), stage.model));
  }
}
