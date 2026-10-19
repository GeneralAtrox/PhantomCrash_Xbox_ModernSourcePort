#include "desktop_online_factory.hpp"
#include "desktop_host.hpp"
#include "online/wire.hpp"
#include "phantomcrash/robot_combat.hpp"
#include <bit>
#include <iostream>
#include <stdexcept>

namespace phantomcrash {
namespace {
void check(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(std::string("Online factory check: ") + message);
  }
}

void vector(online::WireWriter &out, Vec3 value) {
  out.f32(value.x);
  out.f32(value.y);
  out.f32(value.z);
}

void matrix(online::WireWriter &out, const Mat4 &value) {
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      out.f32(value.at(row, column));
    }
  }
}

// Exact observations, not a complete fingerprint of private simulation state.
auto observe(const CoopMatch &match) {
  online::WireWriter out;
  out.u64(std::bit_cast<std::uint64_t>(match.elapsed));
  for (const auto &player : match.players) {
    out.u32(static_cast<std::uint32_t>(player.actor));
    out.u64(static_cast<std::uint64_t>(player.bounty));
  }
  for (const auto &scene : match.rider_scenes) {
    out.u8(bool(scene));
    if (scene) {
      out.u8(scene->active());
      out.u8(scene->scripted());
      out.u8(static_cast<std::uint8_t>(scene->cue()));
      out.f32(scene->letterbox());
    }
  }
  for (const auto &robot : match.battle->robots()) {
    vector(out, robot.mover.position());
    vector(out, robot.mover.velocity());
    out.f32(robot.mover.heading());
    out.f32(robot.mover.aim_heading());
    out.f32(robot.mover.pitch());
    out.f32(robot.hp);
    out.f32(robot.max_hp);
    out.f32(robot.radius);
    out.f32(robot.shield);
    out.f32(robot.camouflage);
    out.u8(robot.alive);
    out.u8(robot.dying);
    out.u8(robot.piloted);
    check(bool(robot.pose), "A robot has no simulation-owned pose");
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
      out.u32(static_cast<std::uint32_t>(robot.magazines[slot].ammo));
      out.f32(robot.cooldown[slot]);
      out.u32(static_cast<std::uint32_t>(robot.locks[slot]));
      out.f32(robot.lock_held[slot]);
      const auto muzzle = robot.pose->muzzle(slot);
      out.u8(muzzle.has_value());
      if (muzzle) {
        matrix(out, *muzzle);
      }
    }
  }
  for (const auto &shot : match.battle->armory().projectiles()) {
    out.u64(shot.id);
    vector(out, shot.position);
    vector(out, shot.velocity);
    out.f32(shot.age);
  }
  return out.take();
}

online::InputFrame controls(std::uint32_t tick) {
  online::InputFrame frame;
  frame.tick = tick;
  frame.players = 7;
  for (std::uint8_t slot = 0; slot < 3; ++slot) {
    auto &input = frame.inputs[slot];
    input.tick = tick;
    input.player = slot;
    input.control.move = {tick % 80 < 40 ? 0.2F : -0.2F, slot == 1 ? 0.6F : -0.6F};
    input.control.jump = tick % 100 >= 30 && tick % 100 < 50;
    input.control.dash = tick % 90 == 20;
    input.fire.fire = {tick % 40 < 20, tick % 60 < 30, false, false};
    input.fire.camouflage = tick % 80 == 0;
  }
  return frame;
}
} // namespace

void check_online_factory(DesktopHost &host, const DataRoots &roots) {
  auto first_resources = std::make_shared<OnlineBattleResources>(host.renderer(), roots);
  auto second_resources = std::make_shared<OnlineBattleResources>(host.renderer(), roots);
  check(!first_resources->rumbling.quick_rentals.empty(), "No quick rental test fixture");
  const auto &preset = first_resources->rumbling.quick_rentals.front();
  online::BattleSetup setup;
  setup.seed = 314159;
  for (int slot = 0; slot < 3; ++slot) {
    online::BattlePlayer player;
    player.profile.player_name = "Factory player " + std::to_string(slot + 1);
    player.profile.money = 1000000;
    const auto entry =
        prepare_quick_rental(preset, first_resources->contests, first_resources->rules,
                             *first_resources->garage, player.profile);
    check(entry.has_value(), "Rental entry failed");
    player.entry = *entry;
    setup.players[slot] = std::move(player);
  }
  setup.world = setup.players[0]->profile;
  setup.area = setup.players[0]->entry.area;
  setup.contest = setup.players[0]->entry.contest;
  auto first = build_online_battle(first_resources, setup);
  auto second = build_online_battle(second_resources, setup);
  check(observe(*first) == observe(*second), "Independent initial arenas differ");
  const auto initial_digest = online::state_digest(*first);
  check(online::state_digest(*second) == initial_digest,
        "Independent initial state digests differ");
  const auto before = observe(*first);
  auto first_view = make_online_battle_view(first, 0, {});
  auto second_view = make_online_battle_view(second, 2, {});
  check(observe(*first) == before && observe(*second) == before,
        "Constructing a local view changed the initialized simulation");
  check(online::state_digest(*first) == initial_digest &&
            online::state_digest(*second) == initial_digest,
        "Constructing a local view changed hidden simulation state");
  check(first->rider_scenes[0] != second->rider_scenes[0], "Replicas share mutable scenes");
  online::MatchSimulation a(*first, 7, 0, &first_resources->assets);
  online::MatchSimulation b(*second, 7, 0, &second_resources->assets);
  std::unique_ptr<online::Simulation::Checkpoint> halfway;
  constexpr std::uint32_t ticks = 720;
  bool controls_released = false;
  for (std::uint32_t tick = 0; tick < ticks; ++tick) {
    if (tick == ticks / 2) {
      halfway = b.save();
      const auto running = observe(*second);
      const auto running_digest = online::state_digest(*second);
      auto additional_view = make_online_battle_view(second, 1, {});
      additional_view->tick({}, online::tick_seconds); // waits for an attached session
      additional_view->draw(fit_canvas(800, 600));
      additional_view.reset();
      check(observe(*second) == running, "Opening a view restarted a running participant");
      check(online::state_digest(*second) == running_digest,
            "Opening a view changed hidden state of a running participant");
    }
    const auto frame = controls(tick);
    a.step(frame);
    b.step(frame);
    controls_released |= !first->rider_scenes[0]->scripted();
    if (tick % 60 == 0) {
      first_view->draw(fit_canvas(640, 480));
      second_view->draw(fit_canvas(960, 480));
    }
    if (observe(*first) != observe(*second)) {
      throw std::runtime_error("Online factory replicas differ at tick " + std::to_string(tick));
    }
  }
  check(controls_released, "Entrance never released player controls");
  const auto final = observe(*first);
  const auto final_digest = online::state_digest(*first);
  check(online::state_digest(*second) == final_digest, "Final hidden simulation states differ");
  b.restore(*halfway);
  for (std::uint32_t tick = ticks / 2; tick < ticks; ++tick) {
    b.step(controls(tick));
  }
  check(observe(*second) == final, "Factory arena rollback replay diverged");
  check(online::state_digest(*second) == final_digest, "Rollback replay diverged in hidden state");
  first_view.reset();
  second_view.reset();
  check(observe(*first) == final && observe(*second) == final,
        "Destroying a local view changed the shared simulation");
  check(online::state_digest(*first) == final_digest &&
            online::state_digest(*second) == final_digest,
        "Destroying a local view changed hidden simulation state");
  std::cout << "PASS: independent factory arenas, 3 players, 720 ticks, distinct local views, "
               "exact combat/poses, hidden-state digests and 360-tick rollback replay\n";
  halfway.reset();
  first.reset();
  second.reset();
  first_resources.reset();
  second_resources.reset();
  check_online_game(host, roots, setup);
  check_online_recovery(host, roots, setup);
}
} // namespace phantomcrash
