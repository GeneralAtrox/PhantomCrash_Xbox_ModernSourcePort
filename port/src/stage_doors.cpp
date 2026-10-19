// The gates' doors (stage objects of type 6: 0x072f85, 0x0731c5, 0x073430,
// driven by 0x070667, 0x0707ae and 0x06f82e).
#include "phantomcrash/stage_doors.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

using json::JsonValue;
using json::required;

constexpr std::size_t triangle_floats = 9;

// The original's rotation matrices (0x0dcabd, 0x0dcb37, 0x0dcbb2), for row
// vectors.
Mat4 rotation_x(const float angle) {
  Mat4 out;
  out.at(1, 1) = std::cos(angle);
  out.at(1, 2) = std::sin(angle);
  out.at(2, 1) = -std::sin(angle);
  out.at(2, 2) = std::cos(angle);
  return out;
}

Mat4 rotation_y(const float angle) {
  Mat4 out;
  out.at(0, 0) = std::cos(angle);
  out.at(0, 2) = -std::sin(angle);
  out.at(2, 0) = std::sin(angle);
  out.at(2, 2) = std::cos(angle);
  return out;
}

Mat4 rotation_z(const float angle) {
  Mat4 out;
  out.at(0, 0) = std::cos(angle);
  out.at(0, 1) = std::sin(angle);
  out.at(1, 0) = -std::sin(angle);
  out.at(1, 1) = std::cos(angle);
  return out;
}

Vec3 vec3(const JsonValue &value, const char *name) {
  const auto v = json::numbers<3>(required(value, name), name);
  return {v[0], v[1], v[2]};
}

DoorLeaf parse_leaf(const JsonValue &value) {
  DoorLeaf leaf;
  leaf.mesh = static_cast<int>(required(value, "mesh").number());
  leaf.swing = static_cast<float>(value.number_or("swing", leaf.swing));
  for (const JsonValue &triangle : required(value, "collision").array()) {
    const auto &v = triangle.array();
    if (v.size() != triangle_floats) {
      throw std::runtime_error("a door triangle needs nine numbers");
    }
    std::array<Vec3, 3> points;
    for (std::size_t corner = 0; corner < 3; ++corner) {
      points[corner] = {static_cast<float>(v[corner * 3].number()),
                        static_cast<float>(v[corner * 3 + 1].number()),
                        static_cast<float>(v[corner * 3 + 2].number())};
    }
    leaf.collision.push_back(points);
  }
  return leaf;
}

// 0x070b80: x, then y, then z rotation, then the position.
Mat4 door_base(const DoorFrame &frame) {
  return rotation_x(frame.rotation.x) * rotation_y(frame.rotation.y) *
         rotation_z(frame.rotation.z) * translation(frame.position);
}

StageCollision leaf_walls(const DoorFrame &frame, const Mat4 &base) {
  std::vector<CollisionTriangle> walls;
  for (const DoorLeaf &leaf : frame.leaves) {
    for (const auto &local : leaf.collision) {
      CollisionTriangle triangle;
      for (std::size_t corner = 0; corner < 3; ++corner) {
        triangle.points[corner] = transform_point(local[corner], base);
      }
      const auto &p = triangle.points;
      triangle.normal = normalize(cross(p[1] - p[0], p[2] - p[0]));
      walls.push_back(triangle);
    }
  }
  return StageCollision(std::move(walls), {});
}

} // namespace

std::vector<DoorFrame> parse_stage_doors(const JsonValue &document, const int stage) {
  std::vector<DoorFrame> doors;
  const JsonValue *stages = document.find("doors");
  const JsonValue *list = stages != nullptr ? stages->find(std::to_string(stage)) : nullptr;
  if (list == nullptr) {
    return doors;
  }
  try {
    for (const JsonValue &value : list->array()) {
      DoorFrame door;
      door.gate = static_cast<int>(required(value, "gate").number());
      door.position = vec3(value, "position");
      door.rotation = vec3(value, "rotation");
      const auto &leaves = required(value, "leaves").array();
      if (leaves.size() != door.leaves.size()) {
        throw std::runtime_error("a door needs two leaves");
      }
      for (std::size_t leaf = 0; leaf < door.leaves.size(); ++leaf) {
        door.leaves[leaf] = parse_leaf(leaves[leaf]);
      }
      doors.push_back(std::move(door));
    }
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("doors: ") + error.what());
  }
  return doors;
}

DoorTuning parse_door_tuning(const JsonValue &value) {
  DoorTuning t;
  const auto number = [&](const char *key, const float fallback) {
    return static_cast<float>(value.number_or(key, fallback));
  };
  t.enabled = value.boolean_or("enabled", t.enabled);
  t.model = value.string_or("model", t.model);
  t.open_angle = number("open_angle", t.open_angle);
  t.open_seconds = number("open_seconds", t.open_seconds);
  t.passable_seconds = number("passable_seconds", t.passable_seconds);
  t.hold_seconds = number("hold_seconds", t.hold_seconds);
  t.close_seconds = number("close_seconds", t.close_seconds);
  t.close_open_seconds = number("close_open_seconds", t.close_open_seconds);
  t.open_sound = value.string_or("open_sound", t.open_sound);
  t.move_sound = value.string_or("move_sound", t.move_sound);
  return t;
}

StageDoors::StageDoors(std::vector<DoorFrame> doors, DoorTuning tuning)
    : tuning_(std::move(tuning)) {
  for (DoorFrame &frame : doors) {
    Door door;
    door.base = door_base(frame);
    door.walls = leaf_walls(frame, door.base);
    door.frame = std::move(frame);
    doors_.push_back(std::move(door));
  }
}

StageDoors::Door *StageDoors::find(const int gate) {
  const auto found = std::ranges::find(doors_, gate, [](const Door &d) { return d.frame.gate; });
  return found == doors_.end() ? nullptr : &*found;
}

const StageDoors::Door *StageDoors::find(const int gate) const {
  const auto found = std::ranges::find(doors_, gate, [](const Door &d) { return d.frame.gate; });
  return found == doors_.end() ? nullptr : &*found;
}

void StageDoors::start_opening(Door &door) {
  // A closing door turns back from where it is (30 - its timer).
  door.timer =
      door.motion == Motion::closing ? std::max(tuning_.close_seconds - door.timer, 0.0F) : 0.0F;
  door.motion = Motion::opening;
  sounds_.push_back({tuning_.move_sound, door.frame.position});
  sounds_.push_back({tuning_.open_sound, door.frame.position});
}

bool StageDoors::open(const int gate) {
  Door *door = find(gate);
  if (door == nullptr || door->motion == Motion::opening) {
    return false;
  }
  start_opening(*door);
  return true;
}

bool StageDoors::close(const int gate) {
  Door *door = find(gate);
  if (door == nullptr || door->motion != Motion::opening ||
      door->timer <= tuning_.close_open_seconds) {
    return false;
  }
  door->motion = Motion::closing;
  door->timer = 0.0F;
  door->passable = false;
  door->locked = true;
  return true;
}

bool StageDoors::idle(const int gate) const {
  const Door *door = find(gate);
  return door == nullptr || door->motion == Motion::idle;
}

void StageDoors::tick(const float seconds) {
  for (Door &door : doors_) {
    switch (door.motion) {
    case Motion::idle:
      break;
    case Motion::opening:
      door.timer += seconds;
      door.open = tuning_.open_seconds > 0.0F
                      ? std::min(door.open + seconds / tuning_.open_seconds, 1.0F)
                      : 1.0F;
      door.passable = door.passable || door.timer > tuning_.passable_seconds;
      if (door.timer >= tuning_.hold_seconds) {
        door.motion = Motion::closing;
        door.timer = 0.0F;
        door.passable = false;
        sounds_.push_back({tuning_.move_sound, door.frame.position});
      }
      break;
    case Motion::closing:
      door.timer += seconds;
      door.open = tuning_.close_seconds > 0.0F
                      ? std::max(door.open - seconds / tuning_.close_seconds, 0.0F)
                      : 0.0F;
      if (door.timer >= tuning_.close_seconds) {
        door.motion = Motion::idle;
        door.open = 0.0F;
        door.locked = false;
      }
      break;
    }
  }
}

unsigned StageDoors::push_sphere(Vec3 &centre, const float radius) {
  unsigned contact = 0;
  for (Door &door : doors_) {
    if (door.passable) {
      continue;
    }
    const unsigned touched = door.walls.push_sphere(centre, radius);
    contact |= touched;
    // 0x06f82e: a touch opens a shut door and turns a closing one back.
    if (touched != 0 && !door.locked && door.motion != Motion::opening) {
      start_opening(door);
    }
  }
  return contact;
}

std::optional<Vec3> StageDoors::segment_hit(const Vec3 from, const Vec3 to) const {
  std::optional<Vec3> nearest;
  for (const Door &door : doors_) {
    if (door.passable) {
      continue;
    }
    if (const auto hit = door.walls.segment_hit(from, to);
        hit && (!nearest || length(*hit - from) < length(*nearest - from))) {
      nearest = hit;
    }
  }
  return nearest;
}

Mat4 StageDoors::leaf_world(const std::size_t door, const std::size_t leaf) const {
  const Door &d = doors_[door];
  const float angle = d.frame.leaves[leaf].swing * tuning_.open_angle * d.open;
  return rotation_y(angle) * d.base;
}

} // namespace phantomcrash
