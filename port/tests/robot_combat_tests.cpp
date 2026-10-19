#include "test_support.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/robot_combat.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace phantomcrash;
namespace {
bool near(Vec3 a, Vec3 b) {
  return length(a - b) < 0.001F;
}

struct Fixture {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("phantomcrash-combat-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  DataRoots roots{path, path / "mods", {path}};
  std::unique_ptr<RobotLibrary> library;
  SkeletonNode body, arms, forearm;
  RobotAssembly assembly;

  void write(const char *name, const std::string &text) {
    const auto file = path / name;
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file) << text;
  }
  Fixture() {
    write("robots/weapon_mounts.json", R"({"weapons":{"gun":{"muzzles":[
      {"position":[0,0,-2],"bone":1,"chain":[]},
      {"position":[1,0,-2],"bone":1,"chain":[]}]}}})");
    write("world/robot_animation.json", R"({"raise_seconds":0.1666666667})");
    std::string motion = R"({"track_count":2,"motions":[)";
    for (int clip = 0; clip < 23; ++clip) {
      if (clip) {
        motion += ',';
      }
      const auto y = clip >= 21 ? "-3" : "-1";
      motion += R"({"frames":30,"tracks":[{"translation":[{"kind":"constant","values":[0]},)";
      motion += std::string(R"({"kind":"constant","values":[)") + y + R"(]},)";
      motion += R"({"kind":"constant","values":[0]}]},{}]})";
    }
    write("arm.json", motion + "]}");
    library = std::make_unique<RobotLibrary>(roots);
    body.flags = node_turns_with_aim;
    body.bone = 0;
    SkeletonNode attach;
    attach.type = SkeletonNodeType::arms;
    body.children.push_back(attach);
    forearm.bone = 1;
    forearm.track = 1;
    forearm.flags = 0x10;
    forearm.translation = {0, -0.5F, 0};
    arms.bone = 0;
    arms.track = 0;
    arms.flags = 0x40;
    arms.children.push_back(forearm);
    assembly.skeleton = &body;
    assembly.arm_skeletons = {&arms, &arms};
    assembly.arm_motions = {"arm.json", "arm.json"};
    assembly.arm_models = {"gun", "gun"};
  }
  ~Fixture() {
    std::filesystem::remove_all(path);
  }
  std::shared_ptr<RobotCombat> rig() {
    return std::make_shared<RobotCombat>(*library, assembly);
  }
};
} // namespace

TEST_CASE("combat pose interpolation keeps rotations rigid across the half turn") {
  const Mat4 from = rotation_y(radians(170)) * translation({1, 2, 3});
  const Mat4 to = rotation_y(radians(-170)) * translation({3, 4, 5});
  const Mat4 half = blend_pose_transform(from, to, 0.5F);
  CHECK(near(transform_point({}, half), {2, 3, 4}));
  CHECK(near(transform_point({0, 0, -1}, half) - transform_point({}, half), {0, 0, 1}));
}

TEST_CASE("weapon attachments compose their authored node transforms") {
  auto mounts = parse_weapon_mounts(json::parse(R"({"weapons":{"gun":{"muzzles":[
    {"bone":1,"position":[0,0,-2],"chain":[
      {"translation":[1,0,0],"rotation":[0,0,0],"scale":[1,1,1]},
      {"translation":[0,3,0],"rotation":[0,0,0],"scale":[1,1,1]}]}]}}})"));
  const auto &socket = mounts.at("gun").front();
  CHECK(near(transform_point({}, socket.local), {1, 0, -2}));
  CHECK(near(transform_point({}, socket.model), {1, 3, -2}));
}

TEST_CASE("arm combat poses raise independently and release at elapsed-time cadence") {
  Fixture f;
  for (int rate : {30, 60, 144}) {
    auto rig = f.rig();
    RobotMover mover(MovementTuning{});
    mover.place({}, 0);
    std::array<Vec3, 4> targets;
    targets.fill({0, 3.0F, -1000000});
    for (int tick = 0; tick < rate / 2; ++tick) {
      rig->tick(mover, {true, false}, {}, targets, 1.0F / rate);
    }
    CHECK(rig->ready(0));
    CHECK(rig->arm_weight(1) == 0);
    CHECK(near(transform_point({}, rig->pose()[1].bones[1]), {0, 2.5F, 0}));
    REQUIRE(rig->muzzle(0));
    CHECK(near(transform_point({}, *rig->muzzle(0)), {0, 2.5F, -2}));
    rig->advance_muzzle(0);
    CHECK(near(transform_point({}, *rig->muzzle(0)), {1, 2.5F, -2}));
    rig->advance_muzzle(0);
    CHECK(near(transform_point({}, *rig->muzzle(0)), {0, 2.5F, -2}));
    for (int tick = 0; tick < rate / 2; ++tick) {
      rig->tick(mover, {}, {}, targets, 1.0F / rate);
    }
    CHECK(rig->arm_weight(0) == 0);
    rig->tick(mover, {true, true}, {}, targets, 1.0F);
    CHECK(rig->ready(0) && rig->ready(1));
    rig->tick(mover, {true, true}, {true, false}, targets, 1.0F / rate);
    CHECK(!rig->muzzle(0));
    CHECK(rig->arm_weight(0) == 0);
    CHECK(rig->ready(1));
  }
}

TEST_CASE("battle fires from the same tick's visible barrel after raising the arm") {
  Fixture f;
  BattleSettings settings;
  settings.weapons["gun"].speed = 60;
  settings.weapons["gun"].cooldown = 0.01F;
  Battle battle(settings, {});
  battle.add_robot("player", 0, {}, 0, {"gun"}, 100);
  battle.robot(0).pose = f.rig();
  FireControl fire;
  fire.fire[0] = true;
  for (int i = 0; i < 8; ++i) {
    battle.tick({}, fire, 1.0F / 60);
    CHECK(battle.armory().projectiles().empty());
  }
  int fired = 0;
  for (int i = 0; i < 5; ++i) {
    battle.tick({}, fire, 1.0F / 60);
    for (const auto &event : battle.events()) {
      if (event.kind != BattleEventKind::fired) {
        continue;
      }
      const auto &pose = battle.player().pose->pose();
      const Vec3 point{static_cast<float>(fired % 2), 0, -2};
      CHECK(near(event.at, transform_point(point, pose[1].bones[1])));
      const auto &shot = battle.armory().projectiles().back();
      CHECK(near(shot.position - shot.velocity * (1.0F / 60), event.at));
      ++fired;
    }
  }
  CHECK(fired >= 2);
}

TEST_CASE("skin bindings count drawing nodes and skip transform-only ancestors") {
  ModelData model;
  model.nodes.resize(5);
  model.draws.resize(3);
  model.draws[0].node = 2;
  model.draws[1].node = 2; // another material, still the same bone
  model.draws[2].node = 4;
  PosePart bones;
  bones.set[0] = bones.set[1] = true;
  bones.bones[0] = translation({1, 2, 3});
  bones.bones[1] = translation({4, 5, 6});
  std::vector<Mat4> worlds;
  skin_nodes(model, bones, 0, {}, worlds);
  CHECK(near(transform_point({}, worlds[2]), {1, 2, 3}));
  CHECK(near(transform_point({}, worlds[4]), {4, 5, 6}));
}

TEST_CASE("unanimated weapon sections follow their posed ancestor through the model hierarchy") {
  ModelData model;
  model.nodes.resize(4);
  for (int i = 0; i < 4; ++i) {
    ModelDraw draw;
    draw.node = i;
    model.draws.push_back(draw);
  }
  model.nodes[1].parent = 0;
  model.nodes[2].parent = 3; // glTF does not require parents to precede children
  model.nodes[2].parent_local = translation({1, 0, 0});
  model.nodes[3].parent = 1;
  model.nodes[3].parent_local = translation({0, 2, 0});
  PosePart bones;
  bones.set[0] = bones.set[1] = true;
  bones.bones[0] = translation({100, 100, 100});
  bones.bones[1] = rotation_y(1.0F) * translation({4, 5, 6});
  std::vector<Mat4> worlds;
  skin_nodes(model, bones, 0, translation({-10, -10, -10}), worlds);
  CHECK(near(transform_point({}, worlds[1]), {4, 5, 6}));
  CHECK(near(transform_point({}, worlds[2]), transform_point({1, 2, 0}, bones.bones[1])));
  CHECK(near(transform_point({}, worlds[3]), transform_point({0, 2, 0}, bones.bones[1])));
}

TEST_CASE("both original HM10 models retain barrel sections on the animated forearm") {
  const auto roots = DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT);
  for (const char *asset : {"models/binlink/12.gltf", "models/binlink/3.gltf"}) {
    const auto path = roots.find_asset(asset);
    if (!path) {
      continue; // private extracted disc assets
    }
    const auto model = load_model(*path, asset);
    REQUIRE(model.nodes.size() == 4);
    CHECK(model.nodes[1].parent == 0);
    CHECK(model.nodes[2].parent == 1);
    CHECK(model.nodes[3].parent == 1);
    PosePart bones;
    bones.set[0] = bones.set[1] = true;
    bones.bones[1] = rotation_y(0.8F) * translation({8, 6, 2});
    std::vector<Mat4> worlds;
    skin_nodes(model, bones, 0, {}, worlds);
    for (const auto point : {Vec3{}, Vec3{0, 0, 1}}) {
      CHECK(near(transform_point(point, worlds[2]), transform_point(point, worlds[1])));
      CHECK(near(transform_point(point, worlds[3]), transform_point(point, worlds[1])));
    }
  }
}

TEST_CASE("both firing arms follow moving and turning robots without a stale muzzle") {
  Fixture f;
  auto rig = f.rig();
  RobotMover mover(MovementTuning{});
  mover.place({4, 0, 9}, radians(70));
  RobotControl control;
  control.move = {0.4F, 1.0F};
  control.twist = 0.5F;
  control.pitch = 0.5F;
  std::array<Vec3, 4> targets;
  targets.fill({-30, 12, -40});
  for (int tick = 0; tick < 30; ++tick) {
    mover.set_floor(0);
    mover.tick(control, 1.0F / 60);
    rig->tick(mover, {true, true}, {}, targets, 1.0F / 60);
    for (std::size_t side = 0; side < 2; ++side) {
      REQUIRE(rig->muzzle(side));
      const auto &bone = rig->pose()[side + 1].bones[1];
      const auto muzzle = *rig->muzzle(side);
      CHECK(near(transform_point({}, muzzle), transform_point({0, 0, -2}, bone)));
      if (rig->ready(side)) {
        const Vec3 forward{-muzzle.at(2, 0), -muzzle.at(2, 1), -muzzle.at(2, 2)};
        CHECK(dot(normalize(forward), normalize(targets[side] - transform_point({}, bone))) >
              0.999F);
      }
    }
  }
  CHECK(!near(mover.position(), {4, 0, 9}));
  CHECK(rig->ready(0) && rig->ready(1));
}

TEST_CASE("a wall between the robot and its barrel prevents firing through geometry") {
  Fixture f;
  BattleSettings settings;
  settings.weapons["gun"].speed = 60;
  CollisionTriangle wall{{Vec3{-20, -20, -1}, Vec3{20, -20, -1}, Vec3{0, 20, -1}}, {0, 0, 1}, 0};
  Battle battle(settings, {});
  battle.set_stage(StageCollision({wall}, {}));
  battle.add_robot("player", 0, {}, 0, {"gun"}, 100);
  battle.set_radius(0, 0.01F);
  battle.robot(0).pose = f.rig();
  FireControl fire;
  fire.fire[0] = true;
  for (int tick = 0; tick < 20; ++tick) {
    battle.tick({}, fire, 1.0F / 60);
    for (const auto &event : battle.events()) {
      CHECK(event.kind != BattleEventKind::fired);
    }
    CHECK(battle.armory().projectiles().empty());
  }
  CHECK(battle.player().pose->ready(0));
  REQUIRE(battle.player().pose->muzzle(0));
  CHECK(transform_point({}, *battle.player().pose->muzzle(0)).z < -1);
}
