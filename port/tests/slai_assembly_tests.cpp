#include "test_support.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/robot.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/robot_load.hpp"

using namespace phantomcrash;

namespace {
int count_draw_bones(const SkeletonNode &node) {
  int count = node.bone >= 0 ? 1 : 0;
  for (const auto &child : node.children) {
    count += count_draw_bones(child);
  }
  return count;
}
} // namespace

TEST_CASE("imported wheel assemblies load every rig and activate their movement data") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto manifest = roots.find_asset("slai/wheels.json");
  if (!manifest) {
    return; // Disc-derived mod content is not bundled with the source.
  }
  const auto list = json::read_file(*manifest);
  const auto parts = parse_part_tables(json::read_file(roots.asset("tables/parts.json")));
  const auto catalogue =
      parse_robot_catalogue(json::read_file(roots.asset("robots/catalogue.json")));
  const auto skeletons =
      parse_robot_skeletons(json::read_file(roots.asset("robots/skeletons.json")));
  REQUIRE(list.find("parts") != nullptr);
  for (const auto &entry : list.find("parts")->array()) {
    const int id = static_cast<int>(entry.number_or("part", -1));
    REQUIRE(id >= 0 && id < static_cast<int>(parts.legs.size()));
    const auto &legs = parts.legs[static_cast<std::size_t>(id)];
    REQUIRE(legs.wheels.has_value());
    REQUIRE(!entry.find("wheel_nodes")->array().empty());
    for (int body = 0; body < 5; ++body) {
      RobotParts build;
      build.body = body;
      build.legs = id;
      const auto assembled = assemble_robot(catalogue, skeletons, build);
      REQUIRE(assembled.skeleton != nullptr);
      CHECK(count_draw_bones(*assembled.skeleton) < bone_slots);
      const auto model = json::read_file(roots.asset(assembled.legs_model));
      const auto &nodes = model.find("nodes")->array();
      CHECK(count_draw_bones(*assembled.skeleton) >= static_cast<int>(nodes.size()));
      Pose pose;
      PoseInput input;
      input.wheel_turn = 0.5F;
      build_pose(*assembled.skeleton, input, pose);
      int filled = 0;
      for (const bool set : pose[0].set) {
        filled += set;
      }
      CHECK(filled == count_draw_bones(*assembled.skeleton));
    }
    RobotLoadout loadout;
    loadout.body = 0;
    loadout.legs = id;
    const auto movement = loaded_movement(parts, loadout, load_stats(parts, loadout), {});
    CHECK(movement.wheels.has_value());
    CHECK(movement.dash_speed > 0);
  }
}
