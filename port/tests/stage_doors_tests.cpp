// The gates' doors: how they open, hold, close and turn back, what a robot
// walking into one does, the exit's locked close and the shots they stop.

#include "test_support.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "phantomcrash/battle_weapons.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/stage_doors.hpp"

using namespace phantomcrash;

namespace {

constexpr float tick = 1.0F / 30.0F;

// A box's twelve triangles, each wound to face out of it.
std::vector<std::array<Vec3, 3>> box(const Vec3 low, const Vec3 high) {
  const auto corner = [&](const int i) {
    return Vec3{(i & 1) != 0 ? high.x : low.x, (i & 2) != 0 ? high.y : low.y,
                (i & 4) != 0 ? high.z : low.z};
  };
  const Vec3 middle = (low + high) * 0.5F;
  const int faces[6][4] = {{0, 2, 6, 4}, {1, 3, 7, 5}, {0, 1, 5, 4},
                           {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 5, 7, 6}};
  std::vector<std::array<Vec3, 3>> out;
  for (const auto &face : faces) {
    for (const std::array<int, 3> &corners :
         {std::array<int, 3>{face[0], face[1], face[2]}, {face[0], face[2], face[3]}}) {
      std::array<Vec3, 3> t{corner(corners[0]), corner(corners[1]), corner(corners[2])};
      const Vec3 centre = (t[0] + t[1] + t[2]) * (1.0F / 3.0F);
      if (dot(cross(t[1] - t[0], t[2] - t[0]), centre - middle) < 0.0F) {
        std::swap(t[1], t[2]);
      }
      out.push_back(t);
    }
  }
  return out;
}

// Gate 2's door: two leaves across x = 5, one each side of z = 0, at the
// arena's origin.
StageDoors test_doors(const DoorTuning &tuning = {}) {
  DoorFrame door;
  door.gate = 2;
  door.leaves[0] = {5, -1.0F, box({4.5F, 0.0F, 0.0F}, {5.5F, 10.0F, 3.0F})};
  door.leaves[1] = {4, 1.0F, box({4.5F, 0.0F, -3.0F}, {5.5F, 10.0F, 0.0F})};
  return StageDoors({door}, tuning);
}

void run(StageDoors &doors, const int ticks) {
  for (int index = 0; index < ticks; ++index) {
    doors.tick(tick);
  }
}

// A sphere of radius 1 walking into the leaves from outside.
bool blocks(StageDoors &doors) {
  Vec3 centre{6.2F, 2.0F, 1.0F};
  return doors.push_sphere(centre, 1.0F) != 0 && centre.x > 6.4F;
}

// The sounds' names, each checked to be placed at the test door.
std::vector<std::string> sound_names(StageDoors &doors) {
  std::vector<std::string> names;
  for (const WorldSound &sound : doors.take_sounds()) {
    CHECK(sound.at.has_value());
    names.push_back(sound.name);
  }
  return names;
}

} // namespace

TEST_CASE("a door opens over a second, lets robots by, holds and closes by itself") {
  StageDoors doors = test_doors();
  CHECK(doors.idle(2));
  CHECK(doors.idle(0)); // no door at gate 0
  CHECK(!doors.open(0));
  CHECK(doors.open(2));
  CHECK(!doors.open(2)); // already opening
  CHECK((sound_names(doors) == std::vector<std::string>{"door_move", "door_open"}));
  run(doors, 19);
  CHECK(blocks(doors)); // not passable before 20 ticks
  run(doors, 2);
  CHECK(!blocks(doors));
  run(doors, 10); // 31 ticks: fully open
  const Vec3 tip = transform_point({5.0F, 0.0F, 3.0F}, doors.leaf_world(0, 0));
  // Swing -1 turns the leaf away from its partner, toward +z.
  CHECK(std::fabs(std::atan2(tip.z, tip.x) - std::atan2(3.0F, 5.0F) - 0.58F) < 0.01F);
  CHECK(!doors.idle(2));
  run(doors, 120); // 151 ticks: closing
  CHECK((sound_names(doors) == std::vector<std::string>{"door_move"}));
  run(doors, 31);
  CHECK(doors.idle(2));
  const Vec3 back = transform_point({5.0F, 0.0F, 3.0F}, doors.leaf_world(0, 0));
  CHECK(length(back - Vec3{5.0F, 0.0F, 3.0F}) < 0.001F);
}

TEST_CASE("walking into a shut door opens it and a closing one turns back") {
  StageDoors doors = test_doors();
  CHECK(blocks(doors));
  CHECK(!doors.idle(2)); // the bump opened it
  run(doors, 151);       // now closing
  CHECK(blocks(doors));  // and the bump turns it back
  CHECK(doors.take_sounds().size() == 5);
  run(doors, 20);
  CHECK(!blocks(doors));
}

TEST_CASE("the exit shuts only a fully open door, and locks it until it has closed") {
  StageDoors doors = test_doors();
  CHECK(doors.open(2));
  run(doors, 20);
  CHECK(!doors.close(2)); // open 20 ticks only
  run(doors, 15);
  CHECK(doors.close(2));
  CHECK(blocks(doors));
  run(doors, 5);
  CHECK(blocks(doors)); // locked: bumping does not open it again
  run(doors, 26);
  CHECK(doors.idle(2));
  CHECK(blocks(doors)); // unlocked once shut
  CHECK(!doors.idle(2));
}

TEST_CASE("the converter's doors and their tuning parse") {
  const auto document = json::parse(R"({"doors": {"6": [{"gate": 1,
    "position": [1, 2, 3], "rotation": [0, 0.785, 0],
    "leaves": [{"mesh": 5, "swing": -1, "collision": [[0,0,0, 1,0,0, 0,1,0]]},
               {"mesh": 4, "swing": 1, "collision": []}]}]}})");
  const std::vector<DoorFrame> doors = parse_stage_doors(document, 6);
  CHECK(doors.size() == 1);
  CHECK(doors[0].gate == 1);
  CHECK(doors[0].position.z == 3.0F);
  CHECK(doors[0].leaves[0].mesh == 5 && doors[0].leaves[0].swing == -1.0F);
  CHECK(doors[0].leaves[0].collision.size() == 1);
  CHECK(parse_stage_doors(document, 7).empty());
  const DoorTuning tuning = parse_door_tuning(json::parse(R"({"hold_seconds": 2})"));
  CHECK(tuning.hold_seconds == 2.0F);
  CHECK(tuning.open_angle == 0.58F);
}

TEST_CASE("a shut door stops shots; an open one lets them by") {
  StageDoors doors = test_doors();
  WeaponTuning gun;
  gun.speed = 60.0F;
  const std::vector<Target> targets{{{}, 1.0F, 0, true}, {{10.0F, 2.0F, 0.0F}, 1.0F, 1, true}};
  const auto shoot = [&](StageDoors *door) {
    Armory armory;
    armory.fire(gun, 0, 0, {0.0F, 2.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, -1, targets);
    std::vector<WeaponHit> hits;
    for (int index = 0; index < 20; ++index) {
      armory.tick(tick, targets, StageCollision{}, hits, door);
    }
    return std::pair{hits.size(), armory.take_landings()};
  };
  const auto [shut_hits, shut_landings] = shoot(&doors);
  CHECK(shut_hits == 0);
  REQUIRE(shut_landings.size() == 1);
  CHECK(std::fabs(shut_landings.front().at.x - 4.5F) < 0.01F);
  CHECK(doors.open(2));
  run(doors, 30);
  CHECK(shoot(&doors).first == 1);
}
