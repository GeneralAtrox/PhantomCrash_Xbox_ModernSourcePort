#include "test_support.hpp"
#include "phantomcrash/online_prediction.hpp"
#include "phantomcrash/online_state.hpp"
#include "phantomcrash/robot_combat.hpp"
#include "../src/online/wire.hpp"

using namespace phantomcrash;
namespace net = phantomcrash::online;

namespace {
std::unique_ptr<Battle> battle() {
  auto settings = parse_battle_settings(json::parse(R"({
    "arena":{"half_size":100},
    "weapons":{"gun":{"kind":"gun","damage":7,"cooldown":0.08,
      "speed":80,"spread_degrees":8,"rounds":3}}
  })"));
  auto result = std::make_unique<Battle>(settings, EnemyPath{}, 123);
  result->set_cooperative(true);
  for (int i = 0; i < 3; ++i) {
    result->add_robot("player", 0, {static_cast<float>(i * 8), 0, 0}, 0, {"gun", "", "", ""}, 100);
    result->robot(i).piloted = true;
  }
  result->add_robot("enemy", 1, {0, 0, -30}, 0, {}, 1000);
  SupplySettings supply;
  SupplyRoute route;
  route.segments = {{{Vec3{-10, 10, -10}, Vec3{0, 10, -10}, Vec3{10, 10, 0}, Vec3{10, 10, 10}}}};
  supply.routes.push_back(route);
  result->set_supply(supply);
  return result;
}

void vector(net::WireWriter &out, Vec3 value) {
  out.f32(value.x);
  out.f32(value.y);
  out.f32(value.z);
}

// Exact observed combat fields for these rollback tests. This is deliberately
// not presented as the complete future-affecting gameplay-state fingerprint.
std::vector<std::uint8_t> observe(const Battle &battle) {
  net::WireWriter out;
  for (const auto &robot : battle.robots()) {
    vector(out, robot.mover.position());
    vector(out, robot.mover.velocity());
    out.f32(robot.mover.heading());
    out.f32(robot.mover.pitch());
    out.f32(robot.mover.mode_seconds());
    out.f32(robot.hp);
    out.f32(robot.camouflage);
    out.u8(robot.alive);
    out.u8(robot.camouflaged);
    out.u32(static_cast<std::uint32_t>(robot.kills));
    for (std::size_t slot = 0; slot < 4; ++slot) {
      out.f32(robot.cooldown[slot]);
      out.f32(robot.lock_held[slot]);
      out.u32(static_cast<std::uint32_t>(robot.locks[slot]));
      out.u32(static_cast<std::uint32_t>(robot.magazines[slot].ammo));
    }
  }
  out.u32(static_cast<std::uint32_t>(battle.armory().projectiles().size()));
  for (const auto &shot : battle.armory().projectiles()) {
    out.u64(shot.id);
    vector(out, shot.position);
    vector(out, shot.velocity);
    out.f32(shot.age);
    out.f32(shot.travelled);
  }
  if (const auto *supply = battle.supply()) {
    vector(out, supply->carrier().position);
    out.f32(supply->carrier().hatch);
    for (const auto &pickup : supply->pickups()) {
      vector(out, pickup.position);
      out.f32(pickup.life);
      out.f32(pickup.wait);
    }
  }
  return out.take();
}

struct BattleSimulation final : net::Simulation {
  struct State final : Checkpoint {
    std::shared_ptr<const Battle> battle;
  };
  std::unique_ptr<Battle> world = battle();
  std::unique_ptr<Checkpoint> save() const override {
    auto state = std::make_unique<State>();
    state->battle = world->checkpoint();
    return state;
  }
  void restore(const Checkpoint &state) override {
    world->restore_checkpoint(*dynamic_cast<const State &>(state).battle);
  }
  void step(const net::InputFrame &frame) override {
    for (std::uint8_t i = 0; i < 3; ++i) {
      const auto &input = frame.inputs[i];
      world->select_player(i);
      world->set_player_target_view(input.view);
      world->set_player_control(i, input.control, input.fire);
    }
    world->select_player(0);
    world->tick({}, {}, net::tick_seconds);
  }
};

net::PlayerInput controls(std::uint32_t tick, std::uint8_t player) {
  net::PlayerInput input;
  input.tick = tick;
  input.player = player;
  input.control.move = {tick > 30 ? 0.2F : 0.0F, player == 1 ? 0.5F : 0.0F};
  input.control.jump = player == 2 && tick >= 10 && tick < 20;
  input.fire.fire[0] = tick < 50;
  input.fire.camouflage = tick == 40;
  return input;
}
} // namespace

TEST_CASE("battle checkpoint owns weapons and supply routes after source destruction") {
  auto source = battle();
  source->set_player_control(0, {}, {{true, false, false, false}, false});
  source->tick({}, {}, net::tick_seconds);
  CHECK(!source->armory().projectiles().empty());
  const auto snapshot = source->checkpoint();
  const auto expected = observe(*source);
  CHECK(observe(*snapshot) == expected);
  CHECK(snapshot->robots().at(0).weapons[0] == &snapshot->settings().weapons.at("gun"));
  source.reset();
  auto restored = battle();
  restored->restore_checkpoint(*snapshot);
  CHECK(observe(*restored) == expected);
  CHECK(restored->robot(0).weapons[0] == &restored->settings().weapons.at("gun"));
  for (const auto &shot : restored->armory().projectiles()) {
    CHECK(shot.weapon == &restored->settings().weapons.at("gun"));
  }
  for (int tick = 0; tick < 90; ++tick) {
    restored->tick({}, {}, net::tick_seconds);
  }
  CHECK(observe(*snapshot) == expected);
}

TEST_CASE("real battle rolls back three delayed players to identical combat observations") {
  BattleSimulation reference, predicted;
  net::Prediction expected(reference, 7, 0, 0, 12);
  net::Prediction network(predicted, 7, 0, 8, 12);
  for (std::uint32_t tick = 0; tick < 90; ++tick) {
    for (std::uint8_t player = 0; player < 3; ++player) {
      expected.submit(controls(tick, player));
    }
    CHECK(expected.advance());
    network.submit(controls(tick, 0));
    if (tick >= 4) {
      network.submit(controls(tick - 4, 1));
      network.submit(controls(tick - 4, 2));
    }
    CHECK(network.advance());
  }
  for (std::uint32_t tick = 86; tick < 90; ++tick) {
    network.submit(controls(tick, 1));
    network.submit(controls(tick, 2));
  }
  network.reconcile();
  CHECK(network.rollback_count() > 0);
  CHECK(observe(*predicted.world) == observe(*reference.world));
}

TEST_CASE("combat pose checkpoint remains independent after the original pose is destroyed") {
  const DataRoots roots{"nonexistent-online-test-data", "nonexistent-online-test-mods", {}};
  RobotLibrary library(roots);
  SkeletonNode root;
  RobotAssembly assembly;
  assembly.skeleton = &root;
  auto source = std::make_shared<RobotCombat>(library, assembly);
  RobotMover mover(MovementTuning{});
  const std::array<Vec3, 4> targets{};
  source->tick(mover, {}, {}, targets, net::tick_seconds);
  RobotCombat copy(*source);
  source.reset();
  const auto before = copy.motion_frame();
  copy.tick(mover, {}, {}, targets, net::tick_seconds);
  CHECK(std::isfinite(before) && std::isfinite(copy.motion_frame()));
}

TEST_CASE("replacing a cooperative actor releases borrowed motion timings with its pose") {
  const DataRoots roots{"nonexistent-online-test-data", "nonexistent-online-test-mods", {}};
  RobotLibrary library(roots);
  SkeletonNode root;
  std::array<std::unique_ptr<Battle>, 2> replicas{battle(), battle()};
  std::array<std::shared_ptr<RobotCombat>, 2> retained;
  for (int index = 0; index < 2; ++index) {
    RobotAssembly assembly;
    assembly.skeleton = &root;
    assembly.timings.resize(1);
    assembly.timings[0].last = index == 0 ? 11.0F : 27.0F;
    retained[index] = std::make_shared<RobotCombat>(library, assembly);
    auto &robot = replicas[index]->robot(0);
    robot.pose = retained[index];
    robot.mover.set_timings(&robot.pose->assembly().timings, index);
    replicas[index]->set_identity(0, "replacement", 0, 100);
    replicas[index]->prepare_deterministic();
    CHECK(!robot.pose);
  }
  // The replacement must not depend on an old animation allocation which a
  // local view may retain on one machine and release on another.
  CHECK(net::state_digest(*replicas[0]) == net::state_digest(*replicas[1]));
  const auto expected = net::state_digest(*replicas[0]);
  retained = {};
  const auto checkpoint = replicas[0]->checkpoint();
  replicas[0]->restore_checkpoint(*checkpoint);
  CHECK(net::state_digest(*replicas[0]) == expected);
}
