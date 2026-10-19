#include "test_support.hpp"
#include "phantomcrash/skeleton.hpp"
#include "phantomcrash/robot_library.hpp"
#include <cmath>

using namespace phantomcrash;

TEST_CASE("source suspension keeps side phase and world axes across repeated poses") {
  SkeletonNode root;
  root.rotation = {0.7F, 0.9F, 0.4F};
  root.translation = {4, 5, 6};
  for (const auto phase : {4.0F, 0.0F}) {
    SkeletonNode leaf;
    leaf.bone = static_cast<int>(root.children.size());
    leaf.translation = {1, 2, 3};
    leaf.source_suspension = SourceSuspension{8, 4, .05F, -.15F, -.1F, phase, 60};
    root.children.push_back(leaf);
  }
  PoseInput input;
  input.root = rotation_x(.5F) * translation({3, 8, 7});
  Pose bind;
  build_pose(root, input, bind);
  // Recovered first side starts at 4, increments before each callback.
  constexpr float first[]{-.0625F, -.025F, .0125F, .05F, .0125F, -.025F, -.0625F, -.1F};
  constexpr float second[]{.0125F, -.025F, -.0625F, -.1F, -.0625F, -.025F, .0125F, .05F};
  for (int tick = 1; tick <= 24; ++tick) {
    input.procedural_seconds = tick / 60.0;
    Pose pose, again;
    build_pose(root, input, pose);
    build_pose(root, input, again);
    for (std::size_t side = 0; side < 2; ++side) {
      const auto expected = side == 0 ? first[(tick - 1) % 8] : second[(tick - 1) % 8];
      for (std::size_t element = 0; element < 16; ++element) {
        const float delta = element == 13 ? expected : 0;
        CHECK(std::abs(pose[0].bones[side].m[element] - bind[0].bones[side].m[element] - delta) <
              1e-5F);
        CHECK(pose[0].bones[side].m[element] == again[0].bones[side].m[element]);
      }
    }
  }
}

TEST_CASE("source suspension clock survives clip changes and resets with the robot") {
  SkeletonNode leaf;
  leaf.bone = 0;
  leaf.source_suspension = SourceSuspension{8, 4, .05F, -.15F, -.1F, 4, 60};
  RobotAssembly assembly;
  assembly.skeleton = &leaf;
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  RobotLibrary library(roots);
  RobotAnimator animator(library, assembly);
  const auto height = [&]() {
    Pose pose;
    animator.pose(Mat4{}, 0, 0, false, pose);
    return pose[0].bones[0].at(3, 1);
  };
  CHECK(height() == 0);
  animator.set_speed(0);
  animator.tick(1.0 / 60);
  CHECK(std::abs(height() + .0625F) < 1e-6F);
  animator.play(28, true, 0, 0, true);
  animator.set_speed(4);
  animator.tick(1.0 / 120);
  CHECK(std::abs(height() + .0625F) < 1e-6F);
  animator.tick(1.0 / 120);
  CHECK(std::abs(height() + .025F) < 1e-6F);
  animator.reset_procedural_clock();
  CHECK(height() == 0);
  animator.tick(1.0 / 60);
  CHECK(std::abs(height() + .0625F) < 1e-6F);
}
