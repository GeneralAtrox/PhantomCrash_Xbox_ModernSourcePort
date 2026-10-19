// Explicit asset-backed rollback check. Run with a checkout/install root that
// contains converted robots; missing assets are a failure, never a skipped test.
#include "phantomcrash/online_prediction.hpp"
#include "phantomcrash/robot_combat.hpp"
#include "../src/online/wire.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace phantomcrash;
namespace net = phantomcrash::online;

namespace {
void vector(net::WireWriter &out, Vec3 value) {
  out.f32(value.x);
  out.f32(value.y);
  out.f32(value.z);
}

void matrix(net::WireWriter &out, const Mat4 &value) {
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      out.f32(value.at(row, column));
    }
  }
}

// Selected exact observations plus every evaluated bone and muzzle. Hidden
// simulation state still needs its separate canonical fingerprint.
std::vector<std::uint8_t> observe(const Battle &battle) {
  net::WireWriter out;
  for (const auto &robot : battle.robots()) {
    vector(out, robot.mover.position());
    vector(out, robot.mover.velocity());
    out.f32(robot.mover.heading());
    out.f32(robot.mover.aim_heading());
    out.f32(robot.mover.pitch());
    out.f32(robot.hp);
    out.f32(robot.camouflage);
    out.f32(robot.pose->motion_frame());
    out.u32(static_cast<std::uint32_t>(robot.pose->motion()));
    for (const auto &part : robot.pose->pose()) {
      for (std::size_t bone = 0; bone < part.set.size(); ++bone) {
        out.u8(part.set[bone]);
        if (part.set[bone]) {
          matrix(out, part.bones[bone]);
        }
      }
    }
    for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
      const auto muzzle = robot.pose->muzzle(slot);
      out.u8(muzzle.has_value());
      if (muzzle) {
        matrix(out, *muzzle);
      }
      out.u8(robot.pose->ready(slot));
      out.f32(robot.cooldown[slot]);
    }
  }
  for (const auto &shot : battle.armory().projectiles()) {
    out.u64(shot.id);
    vector(out, shot.position);
    vector(out, shot.velocity);
    out.f32(shot.age);
  }
  return out.take();
}

struct AssetSimulation final : net::Simulation {
  struct State final : Checkpoint {
    std::shared_ptr<const Battle> battle;
  };
  Battle battle;
  AssetSimulation(RobotLibrary &library, const RobotParts &look)
      : battle(parse_battle_settings(json::parse(R"({
        "arena":{"half_size":100},
        "weapons":{"gun":{"kind":"gun","damage":7,"cooldown":0.08,
          "speed":80,"spread_degrees":8,"rounds":3}}
        })")),
               EnemyPath{}, 123) {
    battle.set_cooperative(true);
    for (int player = 0; player < 3; ++player) {
      battle.add_robot("player", 0, {static_cast<float>(player * 15), 0, 0}, 0,
                       {"gun", "gun", "gun", "gun"}, 1000);
      auto &robot = battle.robot(player);
      robot.piloted = true;
      auto assembly = assemble_robot(library.catalogue(), library.skeletons(), look);
      if (!assembly.skeleton || assembly.timings.empty()) {
        throw std::runtime_error("Converted robot has no skeleton or motion timings");
      }
      battle.set_radius(player, assembly.radius);
      battle.set_movement(player, stride_tuning(library, assembly, battle.settings().movement));
      robot.pose = std::make_shared<RobotCombat>(library, std::move(assembly));
      std::array<Vec3, weapon_slots> targets;
      targets.fill(robot.aim_origin() + heading_forward(0) * robot.pose->aim_distance());
      robot.pose->tick(robot.mover, {}, {}, targets, 0);
    }
  }
  std::unique_ptr<Checkpoint> save() const override {
    auto state = std::make_unique<State>();
    state->battle = battle.checkpoint();
    return state;
  }
  void restore(const Checkpoint &checkpoint) override {
    battle.restore_checkpoint(*dynamic_cast<const State &>(checkpoint).battle);
  }
  void step(const net::InputFrame &frame) override {
    for (int player = 0; player < 3; ++player) {
      const auto &input = frame.inputs[player];
      battle.select_player(player);
      battle.set_player_target_view(input.view);
      battle.set_player_control(player, input.control, input.fire);
    }
    battle.select_player(0);
    battle.tick({}, {}, net::tick_seconds);
  }
};

net::PlayerInput controls(std::uint32_t tick, std::uint8_t player) {
  net::PlayerInput input;
  input.tick = tick;
  input.player = player;
  input.control.move = {tick % 80 < 40 ? 0.2F : -0.2F, player == 1 ? 0.6F : -0.6F};
  input.control.jump = tick % 100 >= 30 && tick % 100 < 50;
  input.control.dash = tick % 90 == 20;
  input.control.reset_twist = tick % 75 == 0;
  input.fire.fire = {tick % 40 < 20, tick % 60 < 30, false, false};
  input.fire.camouflage = tick % 80 == 0;
  return input;
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 2) {
      throw std::runtime_error("Usage: phantomcrash_online_asset_check <game-root>");
    }
    const auto roots = DataRoots::shipped(argv[1]);
    RobotLibrary library(roots);
    if (!library.available()) {
      throw std::runtime_error("Converted robot assets are required");
    }
    const auto document = roots.document("world/training.json");
    const auto *robots = document.find("robots");
    const auto *player = robots ? robots->find("player") : nullptr;
    if (!player) {
      throw std::runtime_error("Training player loadout is required");
    }
    const auto look = parse_robot_parts(*player);
    auto original = std::make_unique<AssetSimulation>(library, look);
    net::InputFrame first;
    first.players = 7;
    for (std::uint8_t slot = 0; slot < 3; ++slot) {
      first.inputs[slot] = controls(0, slot);
    }
    original->step(first);
    const auto snapshot = original->save();
    const auto captured = observe(original->battle);
    original.reset();
    AssetSimulation reference(library, look), delayed(library, look);
    reference.restore(*snapshot);
    delayed.restore(*snapshot);
    if (observe(delayed.battle) != captured) {
      throw std::runtime_error("Pose snapshot differs after original destruction");
    }
    net::Prediction exact(reference, 7, 1, 0, 12), predicted(delayed, 7, 1, 8, 12);
    constexpr std::uint32_t last_tick = 360;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint32_t tick = 1; tick <= last_tick; ++tick) {
      for (std::uint8_t slot = 0; slot < 3; ++slot) {
        exact.submit(controls(tick, slot));
      }
      predicted.submit(controls(tick, 0));
      if (tick > 4) {
        predicted.submit(controls(tick - 4, 1));
        predicted.submit(controls(tick - 4, 2));
      }
      if (!exact.advance() || !predicted.advance()) {
        throw std::runtime_error("Unexpected prediction stall");
      }
    }
    for (std::uint32_t tick = last_tick - 3; tick <= last_tick; ++tick) {
      predicted.submit(controls(tick, 1));
      predicted.submit(controls(tick, 2));
    }
    predicted.reconcile();
    if (!predicted.rollback_count() || observe(delayed.battle) != observe(reference.battle)) {
      throw std::runtime_error("Asset-backed rollback did not converge");
    }
    if (!library.errors().empty()) {
      throw std::runtime_error("Robot motion asset load failed: " + library.errors().front());
    }
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    std::cout << "PASS: 3 posed players, " << last_tick << " ticks, " << predicted.rollback_count()
              << " rollbacks, exact selected combat/bone/muzzle observations; " << elapsed.count()
              << " seconds\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
