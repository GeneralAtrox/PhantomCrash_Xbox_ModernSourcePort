#include "test_support.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/robot.hpp"
#include <cmath>

using namespace phantomcrash;

namespace {
SkeletonNode *control(SkeletonNode &node, const int track) {
  if (node.track == track) {
    return &node;
  }
  for (auto &child : node.children) {
    if (auto *found = control(child, track)) {
      return found;
    }
  }
  return nullptr;
}

void check_body_anchor(const SkeletonNode &rig, const Motion *motion, const float frame) {
  PoseInput input;
  input.legs = {motion, frame};
  input.root = rotation_y(0.7F) * translation({17, 3, -21});
  Pose pose;
  build_pose(rig, input, pose);
  REQUIRE(pose[0].set[0]);
  REQUIRE(pose[0].set[bone_slots - 1]);
  REQUIRE(pose[0].source_model_root.has_value());
  // Authored walking travel stays in the motion data, but cannot displace
  // the rendered robot a second time or jump when the clip wraps.
  for (int axis = 0; axis < 3; ++axis) {
    CHECK(std::abs(pose[0].source_model_root->at(3, axis) - input.root.at(3, axis)) < 0.00001F);
  }
  REQUIRE(pose[0].set[bone_slots - 2]);
  for (std::size_t i = 0; i < pose[0].bones[0].m.size(); ++i) {
    CHECK(std::abs(pose[0].bones[0].m[i] - pose[0].bones[bone_slots - 1].m[i]) < 0.00001F);
    CHECK(std::abs(pose[0].source_model_root->m[i] - pose[0].bones[bone_slots - 2].m[i]) <
          0.00001F);
  }
}
} // namespace

TEST_CASE("imported torsos stay on the animated leg control in every available clip") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto manifest = roots.find_asset("slai/native_parts.json");
  if (!manifest) {
    return;
  }
  const auto parts = json::read_file(*manifest);
  const auto catalogue =
      parse_robot_catalogue(json::read_file(roots.asset("robots/catalogue.json")));
  const auto skeletons =
      parse_robot_skeletons(json::read_file(roots.asset("robots/skeletons.json")));
  int tested = 0;
  for (const auto &entry : parts.find("parts")->array()) {
    if (entry.string_or("category", "") != "leg") {
      continue;
    }
    RobotParts selection;
    selection.body = 0; // Also exercise grafting a Phantom Crash body onto the source legs.
    selection.legs = static_cast<int>(entry.number_or("part", -1));
    const auto assembly = assemble_robot(catalogue, skeletons, selection);
    REQUIRE(assembly.skeleton != nullptr);
    SkeletonNode rig = *assembly.skeleton;
    auto *model_root = control(rig, 0);
    REQUIRE(model_root != nullptr);
    REQUIRE(model_root->source_model_root);
    model_root->bone = bone_slots - 2;
    auto *anchor = control(rig, 1);
    REQUIRE(anchor != nullptr);
    bool mounted = false;
    for (const auto &child : anchor->children) {
      if ((child.flags & node_turns_with_aim) != 0) {
        CHECK(child.bone == 0);
        CHECK(length(child.translation) == 0);
        CHECK(length(child.rotation) == 0);
        mounted = true;
      }
    }
    REQUIRE(mounted);
    anchor->bone = bone_slots - 1; // Observe the leg control independently of the torso.
    check_body_anchor(rig, nullptr, 0);
    REQUIRE(!assembly.leg_motions.empty());
    const auto document = json::read_file(roots.asset(assembly.leg_motions));
    CHECK(document.number_or("source_body_attachment_node", -1) == anchor->track);
    const auto &callbacks = json::required(document, "source_node_callbacks").array();
    REQUIRE(callbacks.size() == 51);
    for (std::size_t track = 0; track < callbacks.size(); ++track) {
      const auto *node = control(rig, static_cast<int>(track));
      const auto &callback = callbacks[track].string();
      const bool rolling = callback == "0x4e5ad0" || callback == "0x4e5b60" ||
                           callback == "0x4e5bf0" || callback == "0x4e5c80" ||
                           callback == "0x4e5d10";
      if (node) {
        CHECK(((node->flags & node_wheel) != 0) == rolling);
        CHECK(node->source_roll.has_value() == rolling);
        if (node->source_roll) {
          CHECK(node->source_roll->spherical == (callback == "0x4e5d10"));
        }
        CHECK(node->source_suspension.has_value() ==
              (callback == "0x4e5e70" || callback == "0x4e5eb0"));
        if (node->source_suspension) {
          CHECK(node->children.empty());
          CHECK(node->source_suspension->phase == (callback == "0x4e5e70" ? 4 : 0));
        }
      } else {
        CHECK(!rolling);
      }
    }
    const auto &source_clips = json::required(document, "source_action_clips").array();
    const auto &clips = json::required(document, "motions").array();
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
      const auto &name = source_clips[static_cast<std::size_t>(original)].string();
      if (name.empty()) {
        CHECK(clips[static_cast<std::size_t>(native)].is_null());
      } else {
        CHECK(clips[static_cast<std::size_t>(native)].string_or("source", "") == name + ".PMT");
      }
    }
    const auto bank = parse_motion_bank(document);
    for (const auto &motion : bank.motions) {
      if (motion) {
        for (const float share : {0.0F, 0.5F, 1.0F}) {
          check_body_anchor(rig, &*motion, motion->frames * share);
        }
      }
    }
    ++tested;
  }
  CHECK(tested == 35);
}
