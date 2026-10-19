#include "test_support.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/rigid_animation.hpp"
#include "phantomcrash/robot.hpp"
#include "phantomcrash/world_model.hpp"
#include <cmath>

using namespace phantomcrash;

TEST_CASE("source leg records select conditional turn timing on mixed body assemblies") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  if (!roots.find_asset("slai/native_parts.json")) {
    return;
  }
  const auto parts = json::read_file(roots.asset("slai/native_parts.json"));
  const auto stats = json::read_file(roots.asset("slai/source_stats.json"));
  const auto catalogue =
      parse_robot_catalogue(json::read_file(roots.asset("robots/catalogue.json")));
  const auto skeletons =
      parse_robot_skeletons(json::read_file(roots.asset("robots/skeletons.json")));
  std::size_t checked = 0;
  for (const auto &part : json::required(parts, "parts").array()) {
    if (part.string_or("category", "") != "leg") {
      continue;
    }
    const auto &record = json::required(stats, "legs")
                             .array()
                             .at(static_cast<std::size_t>(part.number_or("source_record", -1)));
    const int alternate = static_cast<int>(record.number_or("alternate_model", -1));
    const std::array<int, 2> expected =
        alternate == 22 || alternate == 28 ? std::array<int, 2>{10, 10} : std::array<int, 2>{1, 8};
    RobotParts mixed;
    mixed.body = 0;
    mixed.legs = static_cast<int>(part.number_or("part", -1));
    const auto assembly = assemble_robot(catalogue, skeletons, mixed);
    CHECK(assembly.source_turn_frames == expected);
    REQUIRE(assembly.source_transition_frames.size() == 45);
    CHECK(assembly.source_transition_frames[0] == 8);
    CHECK(assembly.source_transition_frames[1] == 2);
    CHECK(assembly.source_transition_frames[10] == 3);
    CHECK(assembly.source_transition_frames[12] == 10);
    ++checked;
  }
  CHECK(checked == 35);
}

TEST_CASE("source body banks select the package authored with the assembled legs") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto file = roots.find_asset("slai/assets/body_animation_banks.json");
  if (!file) {
    return;
  }
  const auto banks = json::read_file(*file);
  const auto parts = json::read_file(roots.asset("slai/native_parts.json"));
  const auto catalogue =
      parse_robot_catalogue(json::read_file(roots.asset("robots/catalogue.json")));
  const auto skeletons =
      parse_robot_skeletons(json::read_file(roots.asset("robots/skeletons.json")));
  std::size_t tested = 0;
  for (const auto &bank : banks.find("banks")->array()) {
    for (const auto &body : parts.find("parts")->array()) {
      if (body.string_or("category", "") != "body" ||
          body.string_or("model", "") + ".PO2" != bank.string_or("source", "")) {
        continue;
      }
      for (const auto &leg_name : bank.find("legs")->array()) {
        for (const auto &leg : parts.find("parts")->array()) {
          if (leg.string_or("category", "") != "leg" ||
              leg.string_or("model", "") + ".PO2" != leg_name.string()) {
            continue;
          }
          RobotParts selection;
          selection.body = static_cast<int>(body.number_or("part", -1));
          selection.legs = static_cast<int>(leg.number_or("part", -1));
          const auto assembly = assemble_robot(catalogue, skeletons, selection);
          const auto definition = json::read_file(roots.asset(assembly.body_model));
          REQUIRE(definition.find("extras") != nullptr);
          CHECK(definition.find("extras")->number_or("package_offset", -1) ==
                bank.number_or("package_offset", -2));
          const auto model = load_model(roots.asset(assembly.body_model), assembly.body_model);
          REQUIRE(model.rig != nullptr);
          CHECK(model.rig->clips.size() == bank.find("clips")->array().size());
          const auto &profile = json::required(bank, "motion_profile");
          const auto &source_clips = json::required(profile, "source_action_clips").array();
          REQUIRE(source_clips.size() == 59);
          REQUIRE(model.rig->motion_clips.size() == 45);
          CHECK(model.rig->preserve_disabled_clips);
          const auto &transitions = json::required(profile, "native_transition_frames").array();
          REQUIRE(model.rig->transition_frames.size() == transitions.size());
          for (std::size_t motion = 0; motion < transitions.size(); ++motion) {
            CHECK(model.rig->transition_frames[motion] ==
                  static_cast<int>(transitions[motion].number()));
          }
          REQUIRE(model.rig->independent_motions.size() == 8);
          REQUIRE(assembly.source_melee_motions.size() == 8);
          for (int action = 35; action < 43; ++action) {
            CHECK(model.rig->motion_clips[action + 2] == source_clips[action].string());
            CHECK(model.rig->independent_motions[action - 35] == action + 2);
            CHECK(assembly.source_melee_motions[action - 35] == action + 2);
          }
          for (const auto &[native, original] : {std::pair{0, 0},
                                                 {1, 2},
                                                 {2, 1},
                                                 {5, 4},
                                                 {8, 44},
                                                 {9, 45},
                                                 {10, 20},
                                                 {11, 22},
                                                 {12, 46},
                                                 {28, 47}}) {
            CHECK(model.rig->motion_clips[static_cast<std::size_t>(native)] ==
                  source_clips[static_cast<std::size_t>(original)].string());
          }
          const auto &attachments = json::required(profile, "attachment_nodes").array();
          REQUIRE(attachments.size() == 16);
          for (std::size_t side = 0; side < 2; ++side) {
            CHECK(model.rig->arm_mount_nodes[side] ==
                  static_cast<int>(attachments[1 - side].number()));
            CHECK(model.rig->shoulder_mount_nodes[side] ==
                  static_cast<int>(attachments[3 - side].number()));
          }
          for (const auto &texture : model.textures) {
            CHECK(roots.find_asset(texture.image).has_value());
          }
          for (const auto &clip : model.rig->clips) {
            std::vector<Mat4> pose;
            std::array<Mat4, 2> mounts;
            std::array<Mat4, 2> shoulders;
            pose_rigid_model(model, clip.name, clip.duration * .5F, false, {}, pose, &mounts, -1,
                             nullptr, &shoulders);
            for (const auto &frames : {mounts, shoulders}) {
              for (const auto &mount : frames) {
                for (float value : mount.m) {
                  CHECK(std::isfinite(value));
                }
              }
            }
            for (const auto &matrix : pose) {
              for (float value : matrix.m) {
                CHECK(std::isfinite(value));
              }
            }
          }
          ++tested;
        }
      }
    }
  }
  CHECK(tested == 35);
  const auto &body = catalogue.body_types.back();
  CHECK(body_model_for_legs(body, -1) == body.model);
}
