#include "test_support.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/source_weapon_actions.hpp"
#include "phantomcrash/rigid_animation.hpp"
#include "phantomcrash/world_model.hpp"
#include "phantomcrash/skeleton.hpp"
#include <cmath>
#include <numbers>

using namespace phantomcrash;
namespace {
bool near(Vec3 a, Vec3 b) {
  return length(a - b) < .0001F;
}
SourceWeaponAction action(int delay, unsigned flags = 0x40) {
  return {flags, {{0x40, delay}}, {{.particle = 21}}};
}
} // namespace

TEST_CASE("source firing counters reset before decrement and postpone smoke during repeated fire") {
  SourceWeaponActionPlayer player({action(3)});
  int starts = 0;
  auto start = [&](const auto &) {
    ++starts;
    return std::uint64_t(1);
  };
  auto dead = [](auto) { return false; };
  player.tick(0x40, 0x40, start, dead);
  player.tick(0, 0x40, start, dead);
  CHECK(starts == 0);
  player.tick(0x40, 0x40, start, dead);
  player.tick(0, 0x40, start, dead);
  CHECK(starts == 0);
  player.tick(0, 0x40, start, dead);
  CHECK(starts == 1);
  player.tick(0, 0x40, start, dead);
  CHECK(starts == 1);
  player.tick(0x40, 0, start, dead);
  player.tick(0, 0x40, start, dead);
  player.tick(0, 0x40, start, dead);
  CHECK(starts == 1);
}

TEST_CASE("source firing suppression follows the last effect handle including disabled returns") {
  auto record = action(1, 0x60);
  record.effects.push_back({.particle = -1});
  SourceWeaponActionPlayer player({record});
  int starts = 0;
  auto start = [&](const SourceActionEffect &effect) {
    ++starts;
    return effect.particle < 0 ? std::uint64_t(0) : std::uint64_t(7);
  };
  auto alive = [](auto key) { return key == 7; };
  player.tick(0x40, 0x40, start, alive);
  player.tick(0x40, 0x40, start, alive);
  CHECK(starts == 4); // last disabled descriptor clears the record's handle
  SourceWeaponActionPlayer held({action(1, 0x60)});
  starts = 0;
  held.tick(0x40, 0x40, start, alive);
  held.tick(0x40, 0x40, start, alive);
  CHECK(starts == 1);
  held.tick(0x40, 0x40, start, [](auto) { return false; });
  CHECK(starts == 2);
}

TEST_CASE("source weapon explosions require their bound state and do not use firing counters") {
  const auto bank = json::parse(R"({"records":[{
    "trigger":{"flags":256,"weapon_state_slot":0,"weapon_gates":[{"mask":255,"delay":1}]},
    "effects":[{"particle":26,"alternate_particle":26,"parent_node":0,"position_offset":[0,0,0]}]
  }]})");
  for (int slot = 0; slot < 4; ++slot) {
    const auto actions = parse_source_weapon_actions(bank, slot);
    REQUIRE(actions.size() == 1);
    CHECK(actions[0].weapon_state_slot == slot);
    CHECK(actions[0].effects[0].weapon_state_slot == slot);
    SourceWeaponActionPlayer player(actions);
    std::array<int, 4> states{};
    int starts = 0;
    const auto tick = [&] {
      player.tick(
          0x3c0, 0x3c0,
          [&](const auto &effect) {
            CHECK(effect.particle == 26);
            return std::uint64_t(++starts);
          },
          [](auto) { return false; }, states);
    };
    tick();
    CHECK(starts == 0);
    states[slot] = 1;
    tick();
    CHECK(starts == 0);
    states[(slot + 1) % 4] = source_weapon_explosion_state;
    tick();
    CHECK(starts == 0);
    states[slot] = source_weapon_explosion_state;
    tick();
    tick(); // the source gate is level-triggered; native producer controls its duration
    CHECK(starts == 2);
    states[slot] = 3;
    tick();
    CHECK(starts == 2);
  }
}

TEST_CASE("source action parent masks normalize joints while frozen smoke inherits translation") {
  Mat4 joint = rotation_y(std::numbers::pi_v<float> / 2) * translation({10, 20, 30});
  for (int c = 0; c < 3; ++c) {
    joint.at(0, c) *= 2;
    joint.at(1, c) *= 3;
    joint.at(2, c) *= 4;
  }
  const auto smoke = source_action_parent_frame(joint, 0x3a00, {1, 2, 3}, false);
  CHECK(near(transform_point({}, smoke), {11, 22, 33}));
  CHECK(near(transform_point({1, 0, 0}, smoke), {12, 22, 33}));
  const auto attached = source_action_parent_frame(joint, 0x380c, {1, 2, 3}, false);
  CHECK(near(transform_point({}, attached), {13, 22, 29}));
  CHECK(near(transform_point({1, 0, 0}, attached), {13, 22, 28}));
  const auto blade = source_action_parent_frame(joint, 0x7800, {}, true);
  CHECK(near(transform_point({1, 0, 0}, blade), {10, 17, 30}));
  const auto imported = source_action_parent_frame(joint, 0x7800, {1, 2, 3}, true, .1F);
  CHECK(near(transform_point({}, imported), {11.2F, 20.6F, 29.8F}));
  CHECK(near(transform_point({1, 0, 0}, imported), {11.2F, 20.3F, 29.8F}));
  const auto masked = source_action_parent_frame(joint, 0x380c, {1, 2, 3}, false, .1F);
  CHECK(near(transform_point({}, masked), {13, 22, 29}));
}

TEST_CASE("source particle world travel retains mesh scale only for full frame attachments") {
  const Mat4 joint = rotation_y(std::numbers::pi_v<float> / 2) * translation({10, 20, 30});
  SourceGenerator generator;
  generator.flags = 1 | 4;
  generator.stop = 1;
  generator.rate = 1;
  generator.capacity = 1;
  generator.lifetime = 100;
  generator.offset = {10, 0, 0};
  const SourceParticleDefinition definition{{generator}};
  for (const bool full : {false, true}) {
    SourceParticles particles(60, 1);
    SourceParticleParent parent;
    parent.frame = source_action_parent_frame(joint, full ? 0x7800 : 0x380c, {0, 2, 0}, false, .1F);
    parent.scale = .5F;
    particles.start_parented(definition, parent, 1);
    particles.tick(1.001F / 60);
    const auto &particle = particles.emitters()[0].particles[0];
    CHECK(near(particle.position, full ? Vec3{10, 20.2F, 29.5F} : Vec3{10, 22, 25}));
    CHECK(near(particle.quad_axes[0], full ? Vec3{0, 0, -.1F} : Vec3{0, 0, -1}));
    CHECK(particle.scale == .5F);
  }
}

TEST_CASE("registered source weapons expose supported fire and loss descriptors on both sides") {
  const auto root = DataRoots::find_root({std::filesystem::current_path()});
  REQUIRE(root.has_value());
  const auto roots = DataRoots::standard(*root);
  const auto table = json::read_file(roots.asset("slai/native_particles.json"));
  const auto catalogue = json::read_file(roots.asset("robots/catalogue.json"));
  int records = 0, effects = 0, loss_records = 0, deferred = 0;
  for (const auto &route : table.find("weapon_actions")->array()) {
    const bool shoulder = route.string_or("mount", "") == "shoulder";
    const auto bank = json::read_file(roots.asset(route.string_or("bank", "")));
    for (const auto &row : bank.find("records")->array()) {
      for (const auto &effect : row.find("effects")->array()) {
        if (effect.number_or("parent_node", 0) < 0 && effect.number_or("particle", -1) >= 0) {
          ++deferred;
        }
      }
    }
    const auto &type = catalogue.find(shoulder ? "shoulder_types" : "arm_types")
                           ->array()
                           .at(static_cast<std::size_t>(route.number_or("model", -1)));
    for (int side = 0; side < 2; ++side) {
      const auto actions = parse_source_weapon_actions(bank, side + (shoulder ? 2 : 0));
      const auto id = static_cast<int>(type.find("models")->array().at(side).number());
      const auto asset = "models/binlink/" + std::to_string(id) + ".gltf";
      const auto model = load_model(roots.asset(asset), asset);
      REQUIRE(model.rig != nullptr);
      for (const auto &record : actions) {
        if (side == 0) {
          ++records;
          effects += static_cast<int>(record.effects.size());
          if (record.flags == 0x100) {
            ++loss_records;
          }
        }
        for (const auto &effect : record.effects) {
          CHECK(effect.particle == effect.alternate_particle);
          CHECK(effect.front_show == 0);
          CHECK(effect.weapon_state_slot ==
                (record.flags == 0x100 ? side + (shoulder ? 2 : 0) : -1));
          if (effect.particle < 0) {
            continue;
          }
          REQUIRE(effect.parent_node >= 0);
          Mat4 frame;
          std::vector<Mat4> drawing;
          pose_rigid_model(model, model.rig->weapon_clip, .1F, false, Mat4{}, drawing, nullptr,
                           effect.parent_node, &frame);
          for (float value : frame.m) {
            CHECK(std::isfinite(value));
          }
        }
      }
    }
  }
  CHECK(records == 181);
  CHECK(effects == 258);
  CHECK(loss_records == 95);
  CHECK(deferred == 12);
}
