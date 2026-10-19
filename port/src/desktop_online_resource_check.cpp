#include "desktop_tools.hpp"
#include "desktop_battle_view.hpp"
#include "desktop_host.hpp"
#include "desktop_online_assets.hpp"
#include "desktop_online_input_check.hpp"
#include "phantomcrash/added_chips.hpp"
#include "desktop_online_factory.hpp"
#include "desktop_wreck_collision.hpp"
#include "phantomcrash/robot_combat.hpp"
#include "phantomcrash/training_ground.hpp"
#include <bit>
#include <iostream>
#include <stdexcept>
#include <tuple>

namespace phantomcrash {
namespace {
void check(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(std::string("Online resource check: ") + message);
  }
}

json::JsonValue converted(const DataRoots &roots, const char *name) {
  auto table = read_converted_table(roots, name);
  if (!table) {
    throw std::runtime_error(std::string("Online resource check requires ") + name);
  }
  return std::move(*table);
}

void append(std::vector<std::uint32_t> &out, const Mat4 &matrix) {
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      out.push_back(std::bit_cast<std::uint32_t>(matrix.at(row, column)));
    }
  }
}

std::vector<std::uint32_t> drawing(BattleView &view, Battle &battle) {
  view.tick(battle, 0);
  std::vector<WorldItem> items;
  view.add_items(battle, items);
  std::vector<std::uint32_t> out;
  for (const auto &item : items) {
    out.push_back(static_cast<std::uint32_t>(item.model));
    append(out, item.world);
    for (const auto &node : item.node_worlds) {
      append(out, node);
    }
  }
  return out;
}

std::vector<std::uint32_t> collision(const StageCollision &shape) {
  std::vector<std::uint32_t> out;
  for (const auto &triangle : shape.walls()) {
    for (const auto point : triangle.points) {
      for (const auto axis : {point.x, point.y, point.z}) {
        out.push_back(std::bit_cast<std::uint32_t>(axis));
      }
    }
    for (const auto axis : {triangle.normal.x, triangle.normal.y, triangle.normal.z}) {
      out.push_back(std::bit_cast<std::uint32_t>(axis));
    }
    out.push_back(static_cast<std::uint32_t>(triangle.surface));
  }
  return out;
}

RobotWreck capture(const Combatant &robot, const Vec3 offset = {}) {
  RobotWreck wreck;
  wreck.assembly = robot.pose->assembly();
  wreck.pose = robot.pose->pose();
  wreck.shoulders = robot.pose->shoulders();
  wreck.root = rotation_y(robot.mover.heading()) * translation(robot.mover.position() + offset);
  wreck.lost = robot.lost;
  for (auto &part : wreck.pose) {
    part.contacts.clear();
    for (std::size_t bone = 0; bone < part.set.size(); ++bone) {
      if (part.set[bone]) {
        part.bones[bone] = part.bones[bone] * translation(offset);
      }
    }
  }
  for (auto &mount : wreck.shoulders) {
    mount = mount * translation(offset);
  }
  return wreck;
}

auto run_death(CoopMatch &match, online::MatchSimulation &simulation) {
  int created = -1;
  for (std::uint32_t tick = 0; tick < 600; ++tick) {
    online::InputFrame frame;
    frame.tick = tick;
    frame.players = 7;
    for (std::uint8_t player = 0; player < 3; ++player) {
      frame.inputs[player].tick = tick;
      frame.inputs[player].player = player;
    }
    simulation.step(frame);
    const auto &wrecks = match.battle->wrecks().items();
    if (wrecks.empty()) {
      continue;
    }
    if (created < 0) {
      created = static_cast<int>(tick);
      check(!wrecks.front().collision_prepared,
            "Death installed collision inside its creation tick");
    } else {
      check(wrecks.front().collision_prepared, "Death wreck was not prepared on the next tick");
    }
    if (wrecks.front().solid()) {
      return std::tuple{created, tick, collision(*wrecks.front().collision)};
    }
  }
  throw std::runtime_error(
      "Online resource check: death did not create a solid wreck in 600 ticks");
}
} // namespace

int check_online_resources(const DataRoots &roots) {
  DesktopHost host(false, true, std::pair{640, 480});
  WorldRenderer renderer(host.renderer(), roots);
  check(renderer.available(), "Direct3D 11 is required");
  RobotLibrary robots(roots);
  const auto parts = parse_part_tables(converted(roots, "tables/parts.json"));
  const auto garage = parse_garage_tables(converted(roots, "tables/garage.json"));
  const auto contests = parse_contest_tables(converted(roots, "tables/town.json"));
  const auto rules = parse_rumbling_rules(roots.document("world/rumbling.json"));
  const auto stats = load_part_stats(roots);
  const auto training = parse_training(roots.document("world/training.json"));
  const auto drive = parse_test_drive(roots.document("world/test_drive.json"));
  const RegulationCheck regulation{rules, parts, garage, stats};
  const RumblingTables tables{parts, garage, contests,
                              rules, drive,  training.battle.settings.weapons};
  CoopMatch match;
  match.world = std::make_shared<RumblingWorld>(regulation, tables, training.battle.settings,
                                                std::array<RumblingStage, roster_areas>{},
                                                RumblingLive{}, 17);
  match.battle = match.world->lend(0, match.simulation_save);
  check(match.battle != nullptr, "No opening-day roster");
  auto &battle = *match.battle;
  battle.set_cooperative(true);
  for (int player = 0; player < 3; ++player) {
    match.join(player, training.robots.at("player"));
  }
  DesktopMatchAssets assets(robots, renderer);
  online::MatchSimulation simulation(match, 7, 0, &assets);
  for (const auto &robot : battle.robots()) {
    check(robot.pose != nullptr, "A robot has no simulation-owned pose");
  }
  const auto before = match.checkpoint();
  BattleView pose_view(renderer, robots);
  pose_view.add(match.players[0].look);
  battle.robot(match.players[0].actor).pose.reset();
  pose_view.bind(battle);
  check(!battle.robot(match.players[0].actor).pose,
        "A local view installed a simulation-owned replacement pose");
  assets.prepare(match);
  check(battle.robot(match.players[0].actor).pose != nullptr,
        "Simulation did not restore its missing pose");
  match.restore_checkpoint(*before);
  auto wreck = capture(battle.robot(match.players[0].actor));
  const auto expected = build_wreck_collision(renderer, wreck);
  check(!expected.walls().empty(), "Wreck has no opaque collision triangles");
  battle.wrecks().add(wreck);
  // This view has no live actors: its submitted items are just the wrecks.
  BattleView view(renderer, robots);
  const auto first_drawing = drawing(view, battle);
  check(!first_drawing.empty(), "Wreck did not create draw items");
  check(!battle.wrecks().items()[0].collision_prepared,
        "A view installed simulation-owned collision");
  battle.tick({}, {}, online::tick_seconds);
  check(battle.wrecks().items()[0].solid(), "Simulation did not prepare the unburning wreck");
  check(collision(*battle.wrecks().items()[0].collision) == collision(expected),
        "Simulation collision differs from the original posed-model calculation");
  const auto first = match.checkpoint();
  check(battle.wrecks().destroy(0), "Solid wreck could not be destroyed");
  check(drawing(view, battle).empty(), "Destroyed wreck remained visible");
  match.restore_checkpoint(*first);
  check(drawing(view, battle) == first_drawing, "Restored wreck did not regain its drawing");
  match.restore_checkpoint(*before);
  battle.wrecks().add(capture(battle.robot(match.players[0].actor), {100, 0, 0}));
  check(drawing(view, battle) != first_drawing,
        "Same-count rollback retained stale wreck geometry");
  match.restore_checkpoint(*first);
  check(drawing(view, battle) == first_drawing, "Rewinding the replacement kept its drawing");

  const auto actor = match.players[0].actor;
  match.players[0].look = training.robots.at("drone");
  assets.prepare(match);
  check(match.simulation_looks[actor] == match.players[0].look, "Replacement look was not bound");
  const auto changed_pose = battle.robot(actor).pose;
  match.restore_checkpoint(*first);
  const auto restored_pose = battle.robot(actor).pose;
  check(restored_pose != changed_pose, "Rollback retained the replacement pose");
  assets.prepare(match);
  check(battle.robot(actor).pose == restored_pose, "Binding reset a correctly restored pose");

  match.restore_checkpoint(*before);
  for (int index = 0; index < static_cast<int>(battle.robots().size()); ++index) {
    battle.take_out(index);
  }
  // Enter the same death-animation state as shot_down; damage application is
  // covered separately. Advance the actual production death and world code.
  auto &victim = battle.robot(actor);
  victim.dying = true;
  victim.death_seconds = 0;
  victim.mover.destroy();
  std::array<Vec3, weapon_slots> targets;
  targets.fill(victim.aim_origin());
  victim.pose->tick(victim.mover, {}, victim.lost, targets, 0);
  const auto dying = simulation.save();
  const auto death = run_death(match, simulation);
  simulation.restore(*dying);
  check(run_death(match, simulation) == death,
        "Death-to-wreck timing or collision diverged after rollback");
  check(robots.errors().empty(), "Robot animation assets did not all load");
  match.restore_checkpoint(*before);
  check_desktop_online_input(host, roots, renderer, robots, match, training);
  check_online_factory(host, roots);
  std::cout << "PASS: desktop online resources, " << battle.robots().size() << " posed robots, "
            << expected.walls().size() << " wreck triangles; collision and cached-draw rollback; "
            << "death wreck tick " << std::get<0>(death) << ", solid tick " << std::get<1>(death)
            << '\n';
  return 0;
}

} // namespace phantomcrash
