#include "test_support.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/robot.hpp"
#include "phantomcrash/source_motion_actions.hpp"
#include <cmath>

using namespace phantomcrash;

namespace {
struct Fixture {
  SourceMotionActionPlayer player;
  std::vector<int> emitted;
  bool retained{};
  explicit Fixture(unsigned flags = 6, float frame = 10)
      : player({{flags, {0, 1}, frame, {{.particle = 7, .alternate_particle = 8}}}}) {}
  void tick(int action, float frame, float duration = 30, float speed = 1, unsigned variant = 0) {
    player.tick(
        {action, frame, duration, speed, variant},
        [&](const auto &effect) {
          emitted.push_back(effect.particle);
          return std::uint64_t(3);
        },
        [&](auto) { return retained; });
  }
};
void check_controls(const SkeletonNode &node, const PosePart &pose) {
  if (node.track >= 0) {
    REQUIRE(node.track < static_cast<int>(pose.source_controls.size()));
    REQUIRE(pose.source_controls[node.track].has_value());
    const auto &frame = *pose.source_controls[node.track];
    for (const float value : frame.m) {
      CHECK(std::isfinite(value));
    }
    if (node.bone >= 0 && node.bone < bone_slots) {
      REQUIRE(pose.set[node.bone]);
      for (std::size_t i = 0; i < frame.m.size(); ++i) {
        CHECK(std::abs(frame.m[i] - pose.bones[node.bone].m[i]) < .00001F);
      }
    }
  }
  for (const auto &child : node.children) {
    check_controls(child, pose);
  }
}
} // namespace

TEST_CASE("source frame gates cross an authored frame without backfilling first samples") {
  Fixture f;
  f.tick(0, 15);
  CHECK(f.emitted.empty());
  f.tick(47, 0);
  f.tick(0, 0);
  f.tick(0, 9);
  CHECK(f.emitted.empty());
  f.tick(0, 10);
  CHECK(f.emitted.size() == 1);
  f.tick(0, 10);
  f.tick(0, 12);
  CHECK(f.emitted.size() == 1);
  f.tick(1, 0); // another listed action does not reset the record
  f.tick(1, 10);
  CHECK(f.emitted.size() == 2);
}

TEST_CASE("source entry bit wins over authored frame and resets only outside its action list") {
  Fixture f(7, 1000);
  f.tick(0, 0);
  CHECK(f.emitted.size() == 1);
  f.tick(0, 20);
  f.tick(1, 0);
  CHECK(f.emitted.size() == 1);
  f.tick(-1, 0);
  f.tick(1, 30);
  CHECK(f.emitted.size() == 2);
}

TEST_CASE("source frame wrap uses duration and speed and distinguishes a rewind") {
  Fixture wrap(6, 19);
  wrap.tick(0, 18, 20, 3);
  wrap.tick(0, 2, 20, 3);
  CHECK(wrap.emitted.size() == 1);
  Fixture rewind(6, 19);
  rewind.tick(0, 18, 30, 1);
  rewind.tick(0, 2, 30, 1);
  CHECK(rewind.emitted.empty());
  rewind.tick(0, 19, 30, 1);
  CHECK(rewind.emitted.size() == 1);
}

TEST_CASE("live source motion effects suppress record clocks until their last handle retires") {
  Fixture f(38);
  f.tick(0, 0);
  f.tick(0, 10);
  CHECK(f.emitted.size() == 1);
  f.retained = true;
  f.tick(47, 0);
  f.retained = false;
  f.tick(0, 11);
  CHECK(f.emitted.size() == 1); // skipped motion miss did not reset saved frames
  f.tick(47, 0);
  f.tick(0, 0);
  f.tick(0, 10);
  CHECK(f.emitted.size() == 2);
}

TEST_CASE("source motion variants compare exact words and retain authored descriptors") {
  Fixture f(7);
  for (const unsigned variant : {0U, 0x80U, 0x100U, 0x81U}) {
    f.tick(-1, 0);
    f.tick(0, 0, 30, 1, variant);
  }
  CHECK(f.emitted == std::vector<int>({7, 8, 8, 7}));
}

TEST_CASE("source death gate repeats after its frame and can start early from owned effects") {
  SourceMotionActionPlayer player({{6, {46}, 30, {{.particle = 27, .alternate_particle = 27}}}});
  std::size_t owned = 0;
  int starts = 0;
  const auto tick = [&](int action, float frame, int mode = 0) {
    player.tick(
        {action, frame, 100, 1, 0, mode},
        [&](const auto &) {
          ++starts;
          return std::uint64_t(starts);
        },
        [](auto) { return true; },
        [&](int resource) {
          CHECK(resource == 27);
          return owned;
        });
  };
  tick(46, 20);
  CHECK(starts == 0);
  owned = 1;
  tick(46, 21);
  CHECK(starts == 1);
  owned = 0;
  tick(46, 29);
  CHECK(starts == 1);
  tick(46, 30);
  tick(46, 31);
  CHECK(starts == 3);
  tick(0, 40);
  CHECK(starts == 3);
  tick(46, 35); // a first sample past the threshold must fire
  CHECK(starts == 4);
  tick(46, 36, 1); // nonzero bank mode restores the normal crossing gate
  CHECK(starts == 4);
}

TEST_CASE("source bank mode suppresses other descriptors without rewinding trigger clocks") {
  SourceMotionActionPlayer player({{7, {0}, 30, {{.particle = 7}, {.particle = 27}}}});
  std::vector<int> emitted;
  const auto tick = [&](int action, int mode) {
    player.tick(
        {action, 0, 100, 1, 0, mode},
        [&](const auto &effect) {
          emitted.push_back(effect.particle);
          return std::uint64_t(1);
        },
        [](auto) { return false; });
  };
  tick(0, 1);
  CHECK(emitted == std::vector<int>{27});
  tick(0, 0);
  CHECK(emitted.size() == 1);
  tick(-1, 0);
  tick(0, 0);
  CHECK(emitted == std::vector<int>({27, 7, 27}));
}

TEST_CASE("all imported model action parents expose drawing and hierarchy-only leg controls") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto file = roots.find_asset("slai/native_particles.json");
  if (!file) {
    return;
  }
  const auto table = json::read_file(*file);
  const auto &routes = json::required(table, "leg_actions").array();
  REQUIRE(routes.size() == 35);
  const auto catalogue =
      parse_robot_catalogue(json::read_file(roots.asset("robots/catalogue.json")));
  const auto skeletons =
      parse_robot_skeletons(json::read_file(roots.asset("robots/skeletons.json")));
  const auto parts = json::read_file(roots.asset("slai/native_parts.json"));
  int tested = 0;
  std::size_t records = 0;
  std::size_t effects = 0;
  for (const auto &route : routes) {
    for (const auto &part : json::required(parts, "parts").array()) {
      if (part.string_or("category", "") != "leg" ||
          part.number_or("type", -1) != route.number_or("model", -2)) {
        continue;
      }
      RobotParts choice;
      choice.legs = static_cast<int>(part.number_or("part", -1));
      const auto assembly = assemble_robot(catalogue, skeletons, choice);
      REQUIRE(assembly.source_motion_actions.size() == 45);
      CHECK(assembly.source_motion_actions[28] == 47);
      CHECK(assembly.source_motion_actions[3] == -1);
      const auto bank = parse_motion_bank(json::read_file(roots.asset(assembly.leg_motions)));
      PoseInput input;
      input.root = rotation_y(.3F) * translation({12, 3, -41});
      input.legs = {bank.motion(0), 7};
      Pose pose;
      build_pose(*assembly.skeleton, input, pose);
      REQUIRE(!pose[0].source_controls.empty());
      check_controls(*assembly.skeleton, pose[0]);
      const auto actions =
          parse_source_motion_actions(json::read_file(roots.asset(route.string_or("bank", ""))));
      records += actions.size();
      for (const auto &action : actions) {
        effects += action.effects.size();
        for (const auto &effect : action.effects) {
          if (effect.particle == 27) {
            CHECK(effect.part_selector == 0); // constructor uses the leg model object's translation
          } else if (effect.part_selector == 0) {
            REQUIRE(effect.parent_node >= 0);
            REQUIRE(effect.parent_node < static_cast<int>(pose[0].source_controls.size()));
            CHECK(pose[0].source_controls[effect.parent_node].has_value());
          }
        }
      }
      ++tested;
    }
  }
  CHECK(tested == 35);
  CHECK(records == 560);
  CHECK(effects == 935);
  // Reusing a pose for a native rig must not retain imported control frames.
  Pose pose;
  pose[0].source_controls.resize(20);
  REQUIRE(skeletons.legs.front().has_value());
  build_pose(*skeletons.legs.front(), {}, pose);
  CHECK(pose[0].source_controls.empty());
}
